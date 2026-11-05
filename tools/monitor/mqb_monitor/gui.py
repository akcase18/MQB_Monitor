"""PySide6 GUI for the MQB monitor.

A sidebar shell over a shared MonitorEngine, polled on a timer, with five
pages:
  * Signals  -- a user-built watchlist of active catalog signals, decoded live.
  * Traffic  -- rolling decoded UDS message log (request/response, latency),
                with CSV export (decoded or raw frames) via Save Log.
  * Registry -- every signal in signals.json, active and WIP alike -- the
                birds-eye view for reviewing what's actually validated.
                Filter, bulk Toggle Status, or Edit any entry. Also where new
                compound signals are built (CompoundSignalDialog): combine
                two existing direct signals with sum/difference/average/min/
                max, no new firmware code required.
  * Learn    -- unknown DIDs seen on the bus; save one Active (straight into
                the compiled catalog), Internal (compiled for the firmware's
                own use, never offered on the dash) or WIP (offset/scale not
                confirmed yet) via SignalEditorDialog's Status combo.
  * WIP      -- every WIP-status signal, for resuming (keep refining) or
                deleting. Nothing here reaches the compiled catalog until
                its status is set to Active or Internal.

Registry/Learn/WIP all read and write the same two-stage registry (see
catalog.py's module docstring) -- there is no separate WIP file anymore.
"""
from __future__ import annotations

import csv
import dataclasses
import importlib.util
import subprocess
import sys
import time
from pathlib import Path

from PySide6.QtCore import Qt, QSettings, QTimer
from PySide6.QtGui import QColor, QCursor, QGuiApplication
from PySide6.QtWidgets import (
    QAbstractItemView,
    QApplication,
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QFrame,
    QHBoxLayout,
    QHeaderView,
    QInputDialog,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMenu,
    QMessageBox,
    QPlainTextEdit,
    QProgressBar,
    QPushButton,
    QSpinBox,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from .catalog import (
    AUTO_FIELD,
    COMPOUND_TRANSFORMS,
    ENGINE_ECU,
    REPO_ROOT,
    STATUS_LABELS,
    UNITS,
    Catalog,
    Ecu,
    EnumEntry,
    Field,
    Request,
    Signal,
    decode_field,
    decode_raw_payload,
    signal_to_json,
    simplest_field,
    to_imperial,
    unit_decimals,
    unit_label,
)
from .capture import CAPTURE_DIR, CaptureWriter, new_capture_path
from .decode_panel import DecodePanel
from .emulator import FakeCar
from .fake_car_tab import FakeCarTab
from .session import (
    MAX_DISTINCT_SAMPLES,
    MonitorEngine,
    Snapshot,
    UnknownDid,
    fill_wip_ecus,
    frame_note,
)
from .sources import (
    DEFAULT_CAN_BAUD,
    SLCAN_BAUD_INDEX,
    DemoSource,
    FileSource,
    SimDongleSource,
    SlcanSerialSource,
    available_ports,
    guess_teensy_port,
)
from .theme import (
    ACCENT,
    AMBER,
    GREEN,
    MONO_FONT,
    RED,
    TEXT_DIM,
    apply_theme,
    mark_primary,
)

# The ECU picker's presets; anything else is entered as Custom.
ECU_PRESETS: list[tuple[str, Ecu]] = [
    ("Engine", ENGINE_ECU),
    ("Transmission", Ecu(0x7E1, 0x7E9)),
]


def _unit_combo(current: str | None) -> QComboBox:
    """A unit picker listing each unit's description, with the
    signals.json unit key as the item data."""
    combo = QComboBox()
    for info in UNITS:
        combo.addItem(info.description, info.key)
    idx = combo.findData(current) if current else -1
    if idx >= 0:
        combo.setCurrentIndex(idx)
    return combo


def _description_edit(text: str, placeholder: str) -> QPlainTextEdit:
    """A word-wrapping box for the phone-app description, so a long
    sentence stays visible while editing. Tab moves focus as in a line
    edit; read it back with _description_text."""
    edit = QPlainTextEdit(text)
    edit.setPlaceholderText(placeholder)
    edit.setTabChangesFocus(True)
    edit.setMaximumHeight(60)
    return edit


def _description_text(edit: QPlainTextEdit) -> str:
    """The description as one line: the phone app shows a single
    sentence, so typed line breaks collapse to spaces."""
    return " ".join(edit.toPlainText().split())


class _EcuPicker(QWidget):
    """Which module a request goes to: Unknown, a preset, or Custom tx/rx
    CAN ids in hex."""

    CUSTOM = "custom"

    def __init__(self, current: Ecu | None) -> None:
        super().__init__()
        row = QHBoxLayout(self)
        row.setContentsMargins(0, 0, 0, 0)
        self.combo = QComboBox()
        self.combo.addItem("Unknown", None)
        for name, ecu in ECU_PRESETS:
            self.combo.addItem(f"{name} ({ecu.label()})", ecu)
        self.combo.addItem("Custom…", self.CUSTOM)
        self.tx = QLineEdit()
        self.rx = QLineEdit()
        for w, hint in ((self.tx, "tx"), (self.rx, "rx")):
            w.setPlaceholderText(hint)
            w.setMaxLength(3)
            w.setFixedWidth(50)
        row.addWidget(self.combo)
        row.addWidget(self.tx)
        row.addWidget(QLabel("/"))
        row.addWidget(self.rx)
        row.addStretch(1)
        self._custom_widgets = [self.tx, row.itemAt(2).widget(), self.rx]

        idx = self.combo.findData(current) if current else 0
        if current is not None and idx < 0:
            idx = self.combo.findData(self.CUSTOM)
            self.tx.setText(f"{current.tx:03X}")
            self.rx.setText(f"{current.rx:03X}")
        self.combo.setCurrentIndex(max(idx, 0))
        self.combo.currentIndexChanged.connect(self._sync)
        self._sync()

    def _sync(self) -> None:
        custom = self.combo.currentData() == self.CUSTOM
        for w in self._custom_widgets:
            w.setVisible(custom)

    def value(self) -> tuple[Ecu | None, str | None]:
        """The chosen ECU (None = unknown), or an error message."""
        data = self.combo.currentData()
        if data != self.CUSTOM:
            return data, None
        try:
            tx, rx = int(self.tx.text(), 16), int(self.rx.text(), 16)
        except ValueError:
            return None, "Enter the custom ECU's tx and rx CAN ids in hex."
        if not (0 <= tx <= 0x7FF and 0 <= rx <= 0x7FF):
            return None, "ECU CAN ids must be 11-bit (000–7FF)."
        return Ecu(tx, rx), None


def _run_gen_catalog() -> None:
    """Regenerates the firmware catalog. With this app's own Python: a bare
    `python3` can be macOS's older one when the app isn't started from a
    PlatformIO shell."""
    subprocess.run([sys.executable, str(REPO_ROOT / "tools/gen_catalog.py")],
                   check=True, capture_output=True, text=True)


def _gen_catalog_error(e: Exception) -> str:
    """What the script itself said, not just its exit status."""
    if isinstance(e, subprocess.CalledProcessError):
        said = (e.stderr or e.stdout or "").strip()
        if said:
            return said if len(said) <= 600 else "…" + said[-600:]
    return str(e)


_gen_catalog = None


def _firmware_problems(signal: Signal, catalog: Catalog) -> list[str]:
    """What would stop tools/gen_catalog.py compiling `signal` (as its own
    status if that's Active or Internal, else as Active) alongside the
    catalog's other compiled signals -- checked before saving one Active
    or Internal, so a signal the firmware can't take never breaks
    regeneration for every other signal. Only problems involving `signal`
    are returned (its own, or a module id it clashes on)."""
    global _gen_catalog
    if _gen_catalog is None:
        spec = importlib.util.spec_from_file_location(
            "gen_catalog", REPO_ROOT / "tools/gen_catalog.py"
        )
        _gen_catalog = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_gen_catalog)
    probe = signal if signal.in_firmware else dataclasses.replace(signal, status="active")
    others = [s for s in catalog.signals if s.in_firmware and s.id != signal.id]
    problems = _gen_catalog.validate(
        {"signals": [signal_to_json(s) for s in others] + [signal_to_json(probe)]}
    )
    own = f"signal id {signal.id} "
    mine = {r.ecu.label() for r in signal.requests if r.ecu}
    return [
        p for p in problems
        if p.startswith(own) or any(m in p for m in mine)
    ]


def _confirm_firmware_ok(parent, signal: Signal, catalog: Catalog) -> bool:
    """For a signal about to be saved Active or Internal: True if the
    firmware can compile it, else explains why not and returns False."""
    problems = _firmware_problems(signal, catalog)
    if not problems:
        return True
    label = STATUS_LABELS.get(signal.status, "Active") if signal.in_firmware else "Active"
    QMessageBox.warning(
        parent, f"Can't mark {label} yet",
        "The firmware can't compile this signal yet:\n\n"
        + "\n".join(f"• {p.split(': ', 1)[-1]}" for p in problems)
        + "\n\nSave it as WIP for now.",
    )
    return False

def _status_combo(existing: Signal | None) -> QComboBox:
    """The editors' Status picker: WIP for a new signal, else the signal's
    own. Each item's data is the registry status string."""
    combo = QComboBox()
    for status in ("wip", "active", "internal"):
        combo.addItem(STATUS_LABELS[status], status)
    combo.setCurrentIndex(combo.findData(existing.status if existing else "wip"))
    combo.setToolTip(
        "Active: compiled in and offered on the dash. Internal: compiled in "
        "for the firmware's own use (e.g. auto brightness), never offered "
        "as a panel. WIP: not compiled."
    )
    return combo


def _status_color(status: str) -> str:
    return {"active": GREEN, "internal": ACCENT}.get(status, AMBER)


# Row tint colors -- translucent, layered over the dark theme's table rows.
# Alphas tuned for the dark palette specifically: a saturated hue needs more
# opacity over near-black than it did over the old light background to read
# at a glance without washing out the (light) row text.
COLOR_REQUEST = QColor(255, 255, 255, 12)
COLOR_UNKNOWN = QColor(245, 166, 35, 60)
COLOR_NEGATIVE = QColor(239, 68, 68, 60)
COLOR_FRESH = QColor(61, 220, 132, 70)
COLOR_NONE = QColor(0, 0, 0, 0)


def _mono(item: QTableWidgetItem) -> QTableWidgetItem:
    """Applies the tabular/monospace font (see theme.py) to a cell holding a
    number, ID, or hex string -- returns `item` so it can be chained."""
    item.setFont(MONO_FONT)
    return item


def _ecu_text(sig: Signal) -> str:
    """The module(s) a signal's requests go to, "—" while unknown."""
    labels = []
    for req in sig.requests:
        text = req.ecu.label() if req.ecu else "—"
        if text not in labels:
            labels.append(text)
    return "; ".join(labels) or "—"


def _responder_text(u: UnknownDid) -> str:
    return u.ecu.label() if u.ecu else f"?/{u.rx_id:03X}"


def _pick_responder(parent, sig: Signal, snap: Snapshot) -> UnknownDid | None:
    """The live traffic that belongs to `sig`: its own module's when its ECU
    is known, else the only module answering its DID, else the one the user
    picks (None if they cancel or nothing has been seen)."""
    did = sig.requests[0].did
    ecu = sig.requests[0].ecu
    if ecu is not None:
        return snap.unknown.get((ecu.rx, did))
    seen = snap.responders(did)
    if len(seen) <= 1:
        return seen[0] if seen else None
    labels = [f"{_responder_text(u)}  ({u.count} responses)" for u in seen]
    choice, ok = QInputDialog.getItem(
        parent, "Which module?",
        f"DID {did[0]:02X} {did[1]:02X} was answered by {len(seen)} modules. "
        f"Which one is this signal?", labels, 0, False)
    return seen[labels.index(choice)] if ok else None


def _fmt_age(now: float, ts: float | None) -> str:
    if ts is None:
        return "—"
    age = now - ts
    if age < 1.0:
        return f"{age * 1000:.0f} ms"
    return f"{age:.1f} s"


class AddSignalsDialog(QDialog):
    """Multi-select picker of catalog signals not already being watched."""

    def __init__(self, available: list[Signal], parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Add signals to watch")
        self.setMinimumSize(360, 420)
        self.chosen: list[int] = []

        layout = QVBoxLayout(self)
        layout.addWidget(QLabel("Choose signals from the catalog to watch:"))

        self.list = QListWidget()
        self.list.setSelectionMode(QAbstractItemView.ExtendedSelection)
        for sig in sorted(available, key=lambda s: s.id):
            unit = f" ({sig.unit})" if sig.unit != "None" else ""
            item = QListWidgetItem(f"{sig.id:>2}   {sig.name}{unit}")
            item.setData(Qt.UserRole, sig.id)
            self.list.addItem(item)
        self.list.itemDoubleClicked.connect(lambda *_: self._accept())
        layout.addWidget(self.list)

        if not available:
            self.list.addItem("(every catalog signal is already being watched)")
            self.list.setEnabled(False)

        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self._accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    def _accept(self) -> None:
        self.chosen = [i.data(Qt.UserRole) for i in self.list.selectedItems()]
        self.accept()


class SignalsTab(QWidget):
    """A user-built watchlist: starts empty; add/remove signals from the
    catalog to focus on the ones you care about. The chosen set persists
    across launches via QSettings."""

    COLUMNS = ["ID", "Signal", "Value", "Unit", "Level", "Updates", "Latency", "Age"]

    def __init__(self, catalog: Catalog, settings: QSettings) -> None:
        super().__init__()
        self.catalog = catalog
        self.settings = settings
        self._by_id = {s.id: s for s in catalog.signals}
        self._watched: list[int] = []  # signal ids, in display order
        self._row_by_id: dict[int, int] = {}
        self._bars: dict[int, QProgressBar] = {}

        layout = QVBoxLayout(self)

        controls = QHBoxLayout()
        add_btn = QPushButton("Add signals…")
        add_btn.clicked.connect(self._add_dialog)
        self.remove_btn = QPushButton("Remove selected")
        self.remove_btn.setEnabled(False)
        self.remove_btn.clicked.connect(self._remove_selected)
        controls.addWidget(add_btn)
        controls.addWidget(self.remove_btn)
        controls.addStretch(1)
        self.count_label = QLabel()
        controls.addWidget(self.count_label)
        layout.addLayout(controls)

        self.hint = QLabel(
            "No signals yet — click “Add signals…” to choose which UDS signals "
            "to watch from the catalog."
        )
        self.hint.setAlignment(Qt.AlignCenter)
        self.hint.setWordWrap(True)
        self.hint.setStyleSheet("color: gray; padding: 32px;")
        layout.addWidget(self.hint, stretch=1)

        self.table = QTableWidget(0, len(self.COLUMNS))
        self.table.setHorizontalHeaderLabels(self.COLUMNS)
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.Stretch)
        for col, width in {0: 44, 2: 84, 3: 58, 4: 170, 5: 74, 6: 84, 7: 76}.items():
            self.table.setColumnWidth(col, width)
        self.table.itemSelectionChanged.connect(self._on_selection)
        layout.addWidget(self.table, stretch=1)

        self._load_watched()
        self._rebuild()

    # --- watchlist management ---

    def _add_dialog(self) -> None:
        # Only direct (non-composite) signals for now -- values computed from
        # multiple requests (e.g. actual intake pressure) are deliberately
        # excluded until they get their own section.
        available = [
            s for s in self.catalog.signals
            if s.id not in self._watched and not s.is_composite
        ]
        dlg = AddSignalsDialog(available, self)
        if dlg.exec() == QDialog.Accepted and dlg.chosen:
            for sid in dlg.chosen:
                if sid not in self._watched:
                    self._watched.append(sid)
            self._save_watched()
            self._rebuild()

    def _remove_selected(self) -> None:
        rows = {i.row() for i in self.table.selectedItems()}
        ids = {self.table.item(r, 0).data(Qt.UserRole) for r in rows}
        self._watched = [s for s in self._watched if s not in ids]
        self._save_watched()
        self._rebuild()

    def add_watch(self, signal_id: int) -> None:
        """Programmatically start watching a signal (used after learn mode)."""
        if signal_id not in self._watched:
            self._watched.append(signal_id)
            self._save_watched()
            self._rebuild()

    def refresh_catalog(self) -> None:
        """Picks up catalog changes (e.g. a learned signal) so the picker and
        rows stay valid."""
        self._by_id = {s.id: s for s in self.catalog.signals}
        self._watched = [s for s in self._watched if self._is_watchable(s)]
        self._rebuild()

    def _is_watchable(self, sid: int) -> bool:
        sig = self._by_id.get(sid)
        return sig is not None and not sig.is_composite

    def _on_selection(self) -> None:
        self.remove_btn.setEnabled(bool(self.table.selectionModel().selectedRows()))

    # --- table build ---

    def _rebuild(self) -> None:
        self.table.setRowCount(0)
        self._row_by_id.clear()
        self._bars.clear()
        for sid in self._watched:
            sig = self._by_id.get(sid)
            if sig is None:
                continue
            row = self.table.rowCount()
            self.table.insertRow(row)
            self._row_by_id[sid] = row
            id_item = self._set(row, 0, str(sig.id))
            id_item.setData(Qt.UserRole, sig.id)  # for remove-by-selection
            self._set(row, 1, sig.name)
            for col in (2, 3, 5, 6, 7):
                self._set(row, col, "—")
            bar = QProgressBar()
            bar.setTextVisible(False)
            bar.setRange(0, 1000)
            bar.setValue(0)
            self._bars[sid] = bar
            self.table.setCellWidget(row, 4, bar)

        empty = not self._row_by_id
        self.hint.setVisible(empty)
        self.table.setVisible(not empty)
        self.count_label.setText(
            "" if empty else f"{len(self._row_by_id)} watched"
        )

    # Columns holding a number/ID rather than prose -- rendered tabular.
    MONO_COLS = (0, 2, 5, 6, 7)

    def _set(self, row: int, col: int, text: str) -> QTableWidgetItem:
        item = self.table.item(row, col)
        if item is None:
            item = QTableWidgetItem()
            if col in (2, 5, 6, 7):
                item.setTextAlignment(Qt.AlignRight | Qt.AlignVCenter)
            if col in self.MONO_COLS:
                _mono(item)
            self.table.setItem(row, col, item)
        item.setText(text)
        return item

    # --- persistence ---

    def _load_watched(self) -> None:
        raw = self.settings.value("watched_signals", "")
        ids = [int(x) for x in str(raw).split(",") if x.strip().isdigit()]
        self._watched = [i for i in ids if self._is_watchable(i)]

    def _save_watched(self) -> None:
        self.settings.setValue(
            "watched_signals", ",".join(str(i) for i in self._watched)
        )

    def update_view(self, snap: Snapshot, is_metric: bool) -> None:
        now = time.monotonic()
        for sid, row in self._row_by_id.items():
            sig = self._by_id.get(sid)
            reading = snap.readings.get(sid)
            if sig is None or reading is None or reading.value is None:
                continue
            metric_val = reading.value
            if sig.unit == "Enum":
                self._set(row, 2, sig.enum_label(metric_val))
            else:
                shown = metric_val if is_metric else to_imperial(sig.unit, metric_val)
                dec = unit_decimals(sig.unit, is_metric)
                self._set(row, 2, f"{shown:.{dec}f}")
            self._set(row, 3, unit_label(sig.unit, is_metric) or "—")
            self._set(row, 5, str(reading.updates))
            self._set(
                row, 6,
                "—" if reading.latency_ms is None else f"{reading.latency_ms:.1f} ms",
            )
            self._set(row, 7, _fmt_age(now, reading.last_update))

            span = sig.display_max - sig.display_min
            frac = 0.0 if span <= 0 else (metric_val - sig.display_min) / span
            self._bars[sid].setValue(int(max(0.0, min(1.0, frac)) * 1000))

            # Freshly-updated rows briefly tint green.
            fresh = reading.last_update is not None and (now - reading.last_update) < 0.4
            tint = COLOR_FRESH if fresh else COLOR_NONE
            self.table.item(row, 2).setBackground(tint)


class TrafficTab(QWidget):
    """Newest traffic at the top, oldest at the bottom -- scroll down for
    history. New rows are always inserted at row 0, which pushes every
    existing row's index down by one; see update_view for how the frozen
    (auto-scroll off) case compensates the scroll position for that shift so
    a paused view doesn't silently drift the way an append-at-the-bottom
    layout would if it trimmed out from under you."""

    COLUMNS = ["Time", "Type", "CAN ID", "DID", "Frm", "Latency", "Decoded"]
    # Soft cap: while auto-scroll is on and we're following the head (top),
    # trim down to this many rows every tick by dropping the oldest ones off
    # the bottom -- invisible, since nothing below the fold is being looked
    # at. Only enforced in that branch: see update_view. HARD_MAX_ROWS is a
    # much higher backstop so a long session left with auto-scroll off
    # (deliberately frozen to inspect something) doesn't grow the widget
    # large enough to make Qt sluggish; the full capture (up to
    # session.MAX_MESSAGE_LOG) still lives in the engine and is what Save
    # Log exports from, regardless of what's on screen.
    MAX_ROWS = 2000
    HARD_MAX_ROWS = 20_000

    def __init__(self) -> None:
        super().__init__()
        self._last_seq = 0
        self._snap_cache: Snapshot | None = None
        self._started_at = 0.0

        layout = QVBoxLayout(self)
        controls = QHBoxLayout()
        self.pause = QCheckBox("Pause")
        self.autoscroll = QCheckBox("Auto-scroll")
        self.autoscroll.setChecked(True)
        self.autoscroll.setToolTip(
            "Follow new traffic (and keep only the most recent rows on "
            "screen). Turn off to freeze the view so you can scroll back "
            "through earlier traffic -- capture keeps running in the "
            "background either way."
        )
        self.filter = QComboBox()
        self.filter.addItems(["All", "Known only", "Unknown only", "Requests", "Responses"])
        save_btn = QPushButton("Save Log…")
        save_btn.setToolTip(
            "Export the captured traffic to CSV -- decoded UDS messages "
            "(honors the current filter) or raw CAN frames (everything)."
        )
        save_btn.clicked.connect(self._save_log)
        clear = QPushButton("Clear")
        clear.clicked.connect(self._clear)
        controls.addWidget(QLabel("Filter:"))
        controls.addWidget(self.filter)
        controls.addWidget(self.pause)
        controls.addWidget(self.autoscroll)
        controls.addStretch(1)
        controls.addWidget(save_btn)
        controls.addWidget(clear)
        layout.addLayout(controls)

        self.table = QTableWidget(0, len(self.COLUMNS))
        self.table.setHorizontalHeaderLabels(self.COLUMNS)
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.horizontalHeader().setSectionResizeMode(6, QHeaderView.Stretch)
        layout.addWidget(self.table)

    def _clear(self) -> None:
        self.table.setRowCount(0)

    def _passes(self, msg) -> bool:
        f = self.filter.currentText()
        if f == "Known only":
            return msg.known
        if f == "Unknown only":
            return not msg.known and msg.kind != "request"
        if f == "Requests":
            return msg.kind == "request"
        if f == "Responses":
            return msg.kind in ("response", "negative")
        return True

    def update_view(self, snap: Snapshot, started_at: float) -> None:
        # Cached even while paused/no-new-messages so Save Log always has
        # the latest capture to export from.
        self._snap_cache = snap
        self._started_at = started_at

        if self.pause.isChecked():
            return
        new = [m for m in snap.messages if m.seq > self._last_seq]
        if not new:
            return
        self._last_seq = new[-1].seq

        # `new` is oldest-first (arrival order). Inserting each one at row 0,
        # in that order, leaves the batch newest-first on screen -- the last
        # (newest) insertion ends up sitting above everything else.
        inserted = 0
        for msg in new:
            if not self._passes(msg):
                continue
            self.table.insertRow(0)
            did = "—" if msg.kind == "negative" else f"{msg.did[0]:02X} {msg.did[1]:02X}"
            lat = "" if msg.latency_ms is None else f"{msg.latency_ms:.1f} ms"
            type_label = {"request": "req", "response": "resp", "negative": "neg"}[msg.kind]
            cells = [
                f"{msg.timestamp - started_at:8.3f}",
                type_label,
                f"{msg.can_id:03X}",
                did,
                str(msg.n_frames),
                lat,
                msg.note,
            ]
            for col, text in enumerate(cells):
                item = QTableWidgetItem(text)
                if col in (0, 4, 5):
                    item.setTextAlignment(Qt.AlignRight | Qt.AlignVCenter)
                if col in (0, 2, 3, 4, 5):
                    _mono(item)
                self.table.setItem(0, col, item)

            tint = None
            if msg.kind == "request":
                tint = COLOR_REQUEST
            elif msg.kind == "negative":
                tint = COLOR_NEGATIVE
            elif not msg.known:
                tint = COLOR_UNKNOWN
            if tint is not None:
                for col in range(len(cells)):
                    self.table.item(0, col).setBackground(tint)
            inserted += 1

        if self.autoscroll.isChecked():
            # Following the head: trimming old rows off the bottom is
            # invisible since the view is already anchored at the top.
            if self.table.rowCount() > self.MAX_ROWS:
                self.table.setRowCount(self.MAX_ROWS)  # drops the oldest rows
            self.table.scrollToTop()
        else:
            # Frozen view: the `inserted` new rows landed above whatever the
            # user is currently looking at, which -- in Qt's default
            # ScrollPerItem mode, where the scrollbar's value is a row index
            # -- pushes that content to a higher row index without moving
            # the scrollbar. Nudge the scroll position down by the same
            # amount to keep the same rows on screen; skipping this would
            # silently drift the view forward exactly like the untrimmed
            # append-at-the-bottom bug this replaced, just from the other
            # end. Only the much-higher backstop cap applies here.
            if inserted:
                sb = self.table.verticalScrollBar()
                sb.setValue(sb.value() + inserted)
            if self.table.rowCount() > self.HARD_MAX_ROWS:
                self.table.setRowCount(self.HARD_MAX_ROWS)

    def _save_log(self) -> None:
        snap = self._snap_cache
        if snap is None or not (snap.messages or snap.frames):
            QMessageBox.information(self, "Save Log", "No traffic captured yet.")
            return

        path, chosen_filter = QFileDialog.getSaveFileName(
            self,
            "Save Traffic Log",
            str(Path.home() / "mqb_traffic.csv"),
            "Decoded UDS messages (*.csv);;Raw CAN frames (*.csv)",
        )
        if not path:
            return

        try:
            if chosen_filter.startswith("Raw"):
                n = self._write_raw_csv(Path(path))
            else:
                n = self._write_decoded_csv(Path(path))
        except OSError as e:
            QMessageBox.warning(self, "Save Log", f"Couldn't write {path}:\n{e}")
            return

        QMessageBox.information(self, "Save Log", f"Saved {n} rows to {path}")

    def _write_decoded_csv(self, path: Path) -> int:
        """Decoded UDS messages, honoring the current filter -- same content
        as what's on screen, but sourced from the engine's full capture
        (session.MAX_MESSAGE_LOG) rather than whatever survived trimming."""
        n = 0
        with open(path, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(
                ["time_s", "type", "can_id", "did", "frames", "latency_ms", "known", "decoded"]
            )
            for msg in self._snap_cache.messages:
                if not self._passes(msg):
                    continue
                did = "" if msg.kind == "negative" else f"{msg.did[0]:02X} {msg.did[1]:02X}"
                lat = "" if msg.latency_ms is None else f"{msg.latency_ms:.1f}"
                writer.writerow([
                    f"{msg.timestamp - self._started_at:.3f}",
                    msg.kind,
                    f"{msg.can_id:03X}",
                    did,
                    msg.n_frames,
                    lat,
                    msg.known,
                    msg.note,
                ])
                n += 1
        return n

    def _write_raw_csv(self, path: Path) -> int:
        """Every captured raw CAN frame, unfiltered -- the lower-level trace
        (ISO-TP frame-type notes included) for offline/other-tool analysis."""
        n = 0
        with open(path, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["time_s", "direction", "can_id", "dlc", "data_hex", "note"])
            for frame, note in self._snap_cache.frames:
                writer.writerow([
                    f"{frame.timestamp - self._started_at:.3f}",
                    frame.direction,
                    frame.id_str(),
                    len(frame.data),
                    frame.hex_data(),
                    note,
                ])
                n += 1
        return n


NEW_FIELD_TIP = (
    "A new signal reading different bytes of the selected signal's DID "
    "(several values packed into one response). The dash still sends one "
    "request for all of them."
)

COLOR_WIP_ROW = QColor(76, 141, 255, 26)  # faint accent tint: "already has WIP notes"


class LearnTab(QWidget):
    COLUMNS = ["DID", "ECU", "Count", "Last payload", "as u8", "as i16", "Age", "Status"]

    def __init__(self, catalog: Catalog, engine: MonitorEngine, window) -> None:
        super().__init__()
        self.catalog = catalog
        self.engine = engine
        self.window = window
        self._keys: list = []

        layout = QVBoxLayout(self)
        info = QLabel(
            "Unknown DIDs seen on the bus (an OBDEleven query with no catalog "
            "match). Select one and click Add to define it — save it Active "
            "to commit it straight to the catalog, or WIP if you don't have "
            "a confirmed scale/offset yet (see the WIP tab)."
        )
        info.setWordWrap(True)
        layout.addWidget(info)

        self.table = QTableWidget(0, len(self.COLUMNS))
        self.table.setHorizontalHeaderLabels(self.COLUMNS)
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.horizontalHeader().setSectionResizeMode(3, QHeaderView.Stretch)
        # "1234 (17 distinct)"
        self.table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeToContents)
        self.table.doubleClicked.connect(lambda *_: self._add_selected())
        layout.addWidget(self.table)

        btns = QHBoxLayout()
        btns.addStretch(1)
        self.add_btn = QPushButton("Add to Catalog / Save WIP…")
        self.add_btn.clicked.connect(self._add_selected)
        btns.addWidget(self.add_btn)
        layout.addLayout(btns)

    def update_view(self, snap: Snapshot) -> None:
        now = time.monotonic()
        unknown = sorted(snap.unknown.values(), key=lambda u: (u.did, u.rx_id))
        self._keys = [u.key for u in unknown]
        self.table.setRowCount(len(unknown))
        for row, u in enumerate(unknown):
            last = u.last
            raw = decode_raw_payload(last) if last else 0
            i16 = int(raw)
            # Any status here is WIP -- an active-status match wouldn't show
            # up as "unknown" at all (Catalog._reindex is active-only).
            is_wip = bool(self.catalog.find_by_did(u.did, rx_id=u.rx_id))
            distinct = len(u.distinct)
            vals = [
                f"{u.did[0]:02X} {u.did[1]:02X}",
                _responder_text(u),
                f"{u.count} ({distinct} distinct)" if distinct > 1 else str(u.count),
                last.hex(" ") or "—",
                str(last[0]) if last else "—",
                str(i16),
                _fmt_age(now, u.last_seen),
                "WIP saved" if is_wip else "—",
            ]
            for col, text in enumerate(vals):
                item = QTableWidgetItem(text)
                if col in (2, 4, 5):
                    item.setTextAlignment(Qt.AlignRight | Qt.AlignVCenter)
                if col != len(vals) - 1:  # every column but Status is a number/hex
                    _mono(item)
                item.setBackground(COLOR_WIP_ROW if is_wip else COLOR_NONE)
                self.table.setItem(row, col, item)

    def _add_selected(self) -> None:
        row = self.table.currentRow()
        if row < 0 or row >= len(self._keys):
            QMessageBox.information(self, "Learn", "Select an unknown DID first.")
            return
        rx_id, did = self._keys[row]
        entry = self.engine.snapshot().unknown.get((rx_id, did))
        samples = entry.samples if entry else []
        matches = self.catalog.find_by_did(did, rx_id=rx_id)
        existing = matches[0] if matches else None
        dlg = SignalEditorDialog(
            self.catalog, existing=existing, did=None if existing else did,
            live_samples=samples, live_ecu=entry.ecu if entry else None,
            parent=self, engine=self.engine, live_rx=rx_id,
        )
        if dlg.exec() == QDialog.Accepted:
            self.window.on_signal_saved(dlg.saved_signal, dlg.saved_path, dlg.regenerated)
            # Jump to where it landed: Signals if now compiled, WIP if still WIP.
            self.window._select_page(0 if dlg.saved_signal.in_firmware else 4)


class SignalEditorDialog(QDialog):
    """The single editing surface for a registry entry (core/vehicle/data/
    signals.json), in one of four modes:

      * Discover -- `did` given, no `existing`: a freshly-seen unknown DID
        from the Learn tab. Defaults to WIP status.
      * Create -- neither `did` nor `existing`: a signal you already know
        about (e.g. from documentation) that hasn't been seen on the bus
        yet. Shows DID byte1/byte2 entry fields, hidden in the other modes.
      * Edit -- `existing` given: review/refine any registry entry (active
        or WIP) from the Registry or WIP tab. Saving replaces it in place
        via Catalog.update_signal, preserving its id and list position.
      * Sibling -- `sibling_of` given: a NEW signal reading other bytes of
        the same module's DID (a multi-value DID). Same DID and ECU, its
        own field; the dash still sends one request for both.

    The left column is the signal itself; the right is the DecodePanel --
    the payload's bytes, the stored raw -> true points and the solver.

    Status (Active/WIP) is one combo box, not a button choice -- this dialog
    *is* the two-stage toggle, in whichever mode it's opened."""

    def __init__(
        self,
        catalog: Catalog,
        existing: Signal | None = None,
        did: tuple[int, int] | None = None,
        live_samples: list[bytes] | None = None,
        live_ecu: Ecu | None = None,
        parent=None,
        engine: MonitorEngine | None = None,
        live_rx: int | None = None,
        sibling_of: Signal | None = None,
    ) -> None:
        super().__init__(parent)
        self.catalog = catalog
        self.existing = existing
        self.saved_signal: Signal | None = None
        self.saved_path: Path | None = None
        self.regenerated = False

        template = existing or sibling_of
        template_req = template.requests[0] if (template and template.requests) else None
        if existing is not None:
            self._composite = existing.is_composite
            self._did = template_req.did if template_req else None
            self._creating = False
        elif sibling_of is not None:
            self._composite = False
            self._did = template_req.did
            self._creating = False
        elif did is not None:
            self._composite = False
            self._did = did
            self._creating = False
        else:
            self._composite = False
            self._did = None  # entered by the user in the DID fields below
            self._creating = True

        # Merge stored samples with any live ones seen right now: distinct
        # payloads, most recently seen last.
        stored = [bytes.fromhex(h) for h in (template.samples if template else [])]
        merged = list(dict.fromkeys(
            reversed(stored + list(live_samples or []))))[::-1]
        samples = merged[-MAX_DISTINCT_SAMPLES:]
        known_ecu = template_req.ecu if template_req else None
        self._live_rx = live_rx if live_rx is not None else (
            known_ecu.rx if known_ecu else None)

        title_did = f"{self._did[0]:02X} {self._did[1]:02X}" if self._did else None
        if existing is not None:
            self.setWindowTitle(f"Edit {existing.name or ('DID ' + title_did if title_did else 'signal')}")
        elif sibling_of is not None:
            self.setWindowTitle(f"New field from DID {title_did}")
        elif did is not None:
            self.setWindowTitle(f"Learn DID {title_did}")
        else:
            self.setWindowTitle("New Signal")

        outer = QVBoxLayout(self)
        columns = QHBoxLayout()
        outer.addLayout(columns, stretch=1)
        left = QWidget()
        left.setFixedWidth(400)
        columns.addWidget(left, alignment=Qt.AlignTop)
        form = QFormLayout(left)
        form.setContentsMargins(0, 0, 0, 0)
        self._form = form

        if title_did:
            form.addRow(QLabel(f"<b>DID {title_did}</b>"))

        self.status_combo = _status_combo(existing)
        form.addRow("Status", self.status_combo)

        self.name = QLineEdit(existing.name if existing else "")
        self.name.setPlaceholderText("e.g. Oil Pressure (optional while WIP)")
        form.addRow("Name", self.name)
        self.description = _description_edit(
            existing.description if existing else "",
            "One sentence for the phone app, e.g. Engine oil pressure after the filter.",
        )
        form.addRow("Description", self.description)

        self.unit = _unit_combo(existing.unit if existing else None)
        form.addRow("Unit", self.unit)

        self.cycle = QSpinBox()
        self.cycle.setRange(10, 60000)
        self.cycle.setValue(template.cycle_time_ms if template else 100)
        self.cycle.setSuffix(" ms")
        form.addRow("Cycle time", self.cycle)

        self.did_byte1 = self.did_byte2 = None
        if self._creating:
            self.did_byte1 = QLineEdit("00")
            self.did_byte2 = QLineEdit("00")
            for w in (self.did_byte1, self.did_byte2):
                w.setMaxLength(2)
                w.setFixedWidth(50)
            form.addRow("DID (hex)", self._pair(self.did_byte1, "/", self.did_byte2))

        self.ecu = _EcuPicker(known_ecu or live_ecu)
        if self._composite or sibling_of is not None:
            self.ecu.setEnabled(False)
        form.addRow("ECU", self.ecu)

        own_req = existing.requests[0] if (existing and existing.requests) else None
        self.offset = self._dspin(own_req.offset if own_req else 0.0)
        self.scale = self._dspin(own_req.scale if own_req else 1.0)
        self.scale.setDecimals(6)
        self.offset.setDecimals(4)
        form.addRow("Offset", self.offset)
        form.addRow("Scale", self.scale)

        if self._composite:
            for w in (self.offset, self.scale):
                w.setEnabled(False)
            hint = QLabel(
                "This signal combines multiple UDS requests using a "
                "hand-written transform (advanced) — edit its requests[] "
                "directly in signals.json."
            )
            hint.setWordWrap(True)
            hint.setStyleSheet(f"color: {TEXT_DIM}; font-size: 11px;")
            form.addRow("", hint)

        self.dmin = self._dspin(existing.display_min if existing else 0.0)
        self.dmax = self._dspin(existing.display_max if existing else 100.0)
        form.addRow("Display min", self.dmin)
        form.addRow("Display max", self.dmax)

        # Unit = Enum: the value -> label table, in place of a display range.
        self.enum_table = QTableWidget(0, 2)
        self.enum_table.setHorizontalHeaderLabels(["Value", "Label"])
        self.enum_table.verticalHeader().setVisible(False)
        self.enum_table.horizontalHeader().setSectionResizeMode(1, QHeaderView.Stretch)
        self.enum_table.setFixedHeight(130)
        for entry in (existing.enum if existing else []):
            self._add_enum_row(str(entry.value), entry.label)
        add_row = QPushButton("Add row")
        add_row.clicked.connect(lambda: self._add_enum_row("", ""))
        remove_row = QPushButton("Remove row")
        remove_row.clicked.connect(self._remove_enum_row)
        row_btns = QHBoxLayout()
        row_btns.addWidget(add_row)
        row_btns.addWidget(remove_row)
        row_btns.addStretch(1)
        self.enum_box = QWidget()
        enum_layout = QVBoxLayout(self.enum_box)
        enum_layout.setContentsMargins(0, 0, 0, 0)
        enum_layout.addWidget(self.enum_table)
        enum_layout.addLayout(row_btns)
        form.addRow("Enum labels", self.enum_box)

        self.preview = QLabel()
        self.preview.setWordWrap(True)
        form.addRow("Preview", self.preview)

        self.notes = QPlainTextEdit(existing.notes if existing else "")
        self.notes.setPlaceholderText(
            "What you've tried, what still needs verifying, related "
            "signals, where you saw the real value to compare against…"
        )
        self.notes.setMaximumHeight(90)
        form.addRow("Notes", self.notes)

        # Right column: bytes, points, solver.
        others = self._others_on_did(sibling_of)
        live = None
        if engine is not None and self._did is not None:
            live = lambda: engine.latest(self._did, self._current_rx())  # noqa: E731
        self.decode = DecodePanel(
            samples,
            own_req.field if own_req else AUTO_FIELD,
            list(existing.points) if existing else [],
            live=live,
            others=others,
            editable_field=not self._composite,
        )
        columns.addWidget(self.decode, stretch=1)

        bottom = QHBoxLayout()
        self.regen = QCheckBox("Regenerate firmware catalog (run gen_catalog.py)")
        self.regen.setChecked(True)
        bottom.addWidget(self.regen)
        bottom.addStretch(1)
        cancel_btn = QPushButton("Cancel")
        cancel_btn.clicked.connect(self.reject)
        bottom.addWidget(cancel_btn)
        save_btn = QPushButton("Save")
        mark_primary(save_btn)
        save_btn.clicked.connect(self._save)
        save_btn.setAutoDefault(False)
        cancel_btn.setAutoDefault(False)
        bottom.addWidget(save_btn)
        outer.addLayout(bottom)

        for sp in (self.offset, self.scale):
            sp.valueChanged.connect(self._sync_decode)
        self.enum_table.itemChanged.connect(self._sync_decode)
        self.unit.currentIndexChanged.connect(self._on_unit_changed)
        self.decode.fieldChanged.connect(self._update_preview)
        self.decode.samplesChanged.connect(self._update_preview)
        self.decode.fitLinear.connect(self._apply_fit)
        self.decode.fitEnum.connect(self._apply_enum)
        self._on_unit_changed()
        self.resize(1080, 640)

    def _current_rx(self) -> int | None:
        """The module to show live traffic from: the one it was opened with,
        else whatever the ECU picker says now."""
        if self._live_rx is not None:
            return self._live_rx
        ecu, _ = self.ecu.value()
        return ecu.rx if ecu else None

    def _others_on_did(self, sibling_of: Signal | None) -> list[tuple[str, Field]]:
        """Other registry signals reading this same module's DID -- shown
        under the byte strip so a new field doesn't reuse their bytes."""
        if self._did is None:
            return []
        rx = self._live_rx
        own_id = self.existing.id if self.existing else None
        out = []
        for sig in self.catalog.find_by_did(self._did, rx_id=rx):
            if sig.id == own_id:
                continue
            for req in sig.requests:
                if req.did == self._did:
                    out.append((sig.name or f"#{sig.id}", req.field))
        return out

    def _dspin(self, value: float) -> QDoubleSpinBox:
        sp = QDoubleSpinBox()
        sp.setRange(-1e9, 1e9)
        sp.setDecimals(4)
        sp.setValue(value)
        return sp

    def _pair(self, a, label, b) -> QWidget:
        w = QWidget()
        h = QHBoxLayout(w)
        h.setContentsMargins(0, 0, 0, 0)
        h.addWidget(a)
        h.addWidget(QLabel(label))
        h.addWidget(b)
        return w

    def _apply_fit(self, offset: float, scale: float) -> None:
        self.offset.setValue(offset)
        self.scale.setValue(scale)

    def _apply_enum(self, entries: list) -> None:
        """Fills the enum table from a fit: values the points mapped take
        their fitted label; rows for other values stay as they were."""
        current, _ = self._enum_entries()
        fitted = {e.value for e in entries}
        merged = sorted(
            [e for e in current if e.value not in fitted] + list(entries),
            key=lambda e: e.value,
        )
        self.enum_table.blockSignals(True)
        self.enum_table.setRowCount(0)
        for e in merged:
            self._add_enum_row(str(e.value), e.label)
        self.enum_table.blockSignals(False)
        self._sync_decode()

    def _is_enum(self) -> bool:
        return self.unit.currentData() == "Enum"

    def _on_unit_changed(self) -> None:
        is_enum = self._is_enum()
        self._form.setRowVisible(self.enum_box, is_enum)
        self._form.setRowVisible(self.dmin, not is_enum)
        self._form.setRowVisible(self.dmax, not is_enum)
        self._sync_decode()

    def _sync_decode(self) -> None:
        entries, _ = self._enum_entries()
        self.decode.set_decode(self.offset.value(), self.scale.value(),
                               self.unit.currentData(), entries)
        self._update_preview()

    def _add_enum_row(self, value: str, label: str) -> None:
        row = self.enum_table.rowCount()
        self.enum_table.insertRow(row)
        self.enum_table.setItem(row, 0, _mono(QTableWidgetItem(value)))
        self.enum_table.setItem(row, 1, QTableWidgetItem(label))

    def _remove_enum_row(self) -> None:
        row = self.enum_table.currentRow()
        if row >= 0:
            self.enum_table.removeRow(row)
            self._sync_decode()

    def _enum_entries(self) -> tuple[list[EnumEntry], str | None]:
        """The table's rows as entries, or an error message. Fully blank
        rows are skipped."""
        entries: list[EnumEntry] = []
        for row in range(self.enum_table.rowCount()):
            cells = [self.enum_table.item(row, c) for c in (0, 1)]
            value_text, label = (c.text().strip() if c else "" for c in cells)
            if not value_text and not label:
                continue
            try:
                value = int(value_text, 0)
            except ValueError:
                return [], f"Row {row + 1}: value {value_text!r} isn't a whole number."
            if not label:
                return [], f"Row {row + 1}: value {value} needs a label."
            if any(e.value == value for e in entries):
                return [], f"Value {value} is listed twice."
            entries.append(EnumEntry(value, label))
        return entries, None

    def _update_preview(self) -> None:
        newest = self.decode.newest()
        raw = decode_field(newest, self.decode.field()) if newest else None
        if raw is None:
            self.preview.setText("no sample to decode yet" if not newest
                                 else "the newest sample is too short for this field")
            return
        decoded = (raw + self.offset.value()) * self.scale.value()
        if self._is_enum():
            entries, _ = self._enum_entries()
            probe = Signal(0, "", "Enum", 0, "default", 0, 0, enum=entries)
            self.preview.setText(
                f"newest sample decodes to {int(round(decoded))} → "
                f"{probe.enum_label(decoded)}"
            )
            return
        label = unit_label(self.unit.currentData(), True)
        self.preview.setText(f"newest sample decodes to {decoded:.3f} {label}")

    def _resolve_did(self) -> tuple[int, int] | None:
        if self._did is not None:
            return self._did
        try:
            b1 = int(self.did_byte1.text(), 16)
            b2 = int(self.did_byte2.text(), 16)
        except ValueError:
            return None
        if not (0 <= b1 <= 0xFF and 0 <= b2 <= 0xFF):
            return None
        return (b1, b2)

    def _save(self) -> None:
        status = self.status_combo.currentData()
        name = self.name.text().strip()
        if status != "wip" and not name:
            QMessageBox.warning(
                self, "Save",
                f"Give the signal a name before marking it "
                f"{STATUS_LABELS[status]}.",
            )
            return

        samples = self.decode.samples()
        if self._composite:
            requests = list(self.existing.requests)  # untouched -- see hint
        else:
            did = self._resolve_did()
            if did is None:
                QMessageBox.warning(self, "Save", "Enter a valid DID (two hex bytes).")
                return
            ecu, err = self.ecu.value()
            if err:
                QMessageBox.warning(self, "Save", err)
                return
            # A pick that reads exactly what the legacy decode reads is saved
            # as the legacy form, which the firmware already compiles.
            fld = simplest_field(self.decode.field(), samples)
            requests = [
                Request(
                    did[0], did[1], self.offset.value(), self.scale.value(),
                    ecu=ecu, field=fld,
                )
            ]

        enum, err = self._enum_entries()
        if err:
            QMessageBox.warning(self, "Save", err)
            return
        points, err = self.decode.points()
        if err:
            QMessageBox.warning(self, "Save", err)
            return

        signal = Signal(
            id=self.existing.id if self.existing else self.catalog.next_id(),
            name=name,
            unit=self.unit.currentData(),
            cycle_time_ms=self.cycle.value(),
            transform=self.existing.transform if self.existing else "default",
            display_min=self.dmin.value(),
            display_max=self.dmax.value(),
            requests=requests,
            status=status,
            description=_description_text(self.description),
            notes=self.notes.toPlainText().strip(),
            samples=[s.hex() for s in samples],
            enum=enum,
            points=points,
        )

        if status != "wip" and not _confirm_firmware_ok(self, signal, self.catalog):
            return

        if self.existing is not None:
            self.catalog.update_signal(signal)
        else:
            self.catalog.add_signal(signal)
        self.saved_signal = signal
        self.saved_path = self.catalog.save()

        if self.regen.isChecked():
            try:
                _run_gen_catalog()
                self.regenerated = True
            except (subprocess.CalledProcessError, OSError) as e:
                QMessageBox.warning(
                    self, "Regenerate",
                    f"Saved signals.json, but gen_catalog.py failed:\n\n"
                    f"{_gen_catalog_error(e)}",
                )

        self.accept()


class CompoundSignalDialog(QDialog):
    """Builds -- or edits -- a signal whose value is computed from two
    existing DIRECT signals (single request, "default" transform) in the
    registry -- e.g. "boost above ambient" = charge pressure - ambient
    pressure. There's no offset/scale to solve here: each source's own
    (already-calibrated) request is copied over verbatim, so all this dialog
    decides is *which two* and *how to combine them* (one of
    catalog.COMPOUND_TRANSFORMS -- sum/difference/average/min/max), which
    needs no new C++ per signal since those transforms already exist in both
    signal_catalog.cpp and this tool's TRANSFORMS.

    Two modes, like SignalEditorDialog:
      * Create -- `existing` is None: pick two sources from scratch.
      * Edit -- `existing` given (must be composite with a transform in
        COMPOUND_TRANSFORMS -- callers are responsible for routing bespoke-
        transform composites like actual_intake_pressure to SignalEditorDialog
        instead, since those aren't expressible as "two named sources").
        Each side's source picker tries to match `existing`'s stored request
        back to a live signal (by identical DID+offset+scale); if the
        original source was since deleted or edited, that side falls back to
        an "(unchanged)" placeholder holding the request as originally
        saved, so editing metadata never silently changes a source you
        didn't touch.

    A signal built this way reports is_composite == True, so it's excluded
    from the Signals watchlist picker like any other composite signal."""

    def __init__(
        self, catalog: Catalog, existing: Signal | None = None, parent=None,
    ) -> None:
        super().__init__(parent)
        self.catalog = catalog
        self.existing = existing
        self.saved_signal: Signal | None = None
        self.saved_path: Path | None = None
        self.regenerated = False

        self._sources = sorted(
            (s for s in catalog.signals if not s.is_composite),
            key=lambda s: s.name.lower(),
        )

        self.setWindowTitle(
            f"Edit Compound Signal — {existing.name}" if existing else "New Compound Signal"
        )
        self.setMinimumWidth(480)
        form = QFormLayout(self)

        if existing is None and len(self._sources) < 2:
            form.addRow(QLabel(
                "Need at least two direct (non-compound) signals in the "
                "registry before you can combine any — add those first."
            ))
            buttons = QDialogButtonBox(QDialogButtonBox.Close)
            buttons.rejected.connect(self.reject)
            buttons.accepted.connect(self.reject)
            form.addRow(buttons)
            self._disabled = True
            return
        self._disabled = False

        self.status_combo = _status_combo(existing)
        form.addRow("Status", self.status_combo)

        self.name = QLineEdit(existing.name if existing else "")
        self.name.setPlaceholderText("e.g. Boost Above Ambient")
        form.addRow("Name", self.name)
        self.description = _description_edit(
            existing.description if existing else "",
            "One sentence for the phone app, e.g. Boost: intake pressure minus ambient.",
        )
        form.addRow("Description", self.description)

        self.unit = _unit_combo(existing.unit if existing else None)
        form.addRow("Unit", self.unit)

        self.cycle = QSpinBox()
        self.cycle.setRange(10, 60000)
        self.cycle.setValue(existing.cycle_time_ms if existing else 100)
        self.cycle.setSuffix(" ms")
        form.addRow("Cycle time", self.cycle)

        self.op = QComboBox()
        for key, label in COMPOUND_TRANSFORMS.items():
            self.op.addItem(label, userData=key)
        if existing and existing.transform in COMPOUND_TRANSFORMS:
            self.op.setCurrentIndex(self.op.findData(existing.transform))
        self.op.currentIndexChanged.connect(self._update_preview)
        form.addRow("Combine using", self.op)

        self.preview = QLabel()  # created early: setCurrentIndex below fires
        self.preview.setWordWrap(True)  # _update_preview before this row is added

        req_a = existing.requests[0] if (existing and len(existing.requests) > 0) else None
        req_b = existing.requests[1] if (existing and len(existing.requests) > 1) else None
        match_a = self._find_source_match(req_a)
        match_b = self._find_source_match(req_b)
        self._fallback_a = req_a if (req_a is not None and match_a is None) else None
        self._fallback_b = req_b if (req_b is not None and match_b is None) else None

        self.source_a = QComboBox()
        self.source_b = QComboBox()
        self._populate_source_combo(self.source_a, self._fallback_a)
        self._populate_source_combo(self.source_b, self._fallback_b)
        if match_a is not None:
            self.source_a.setCurrentIndex(self.source_a.findData(match_a.id))
        if match_b is not None:
            self.source_b.setCurrentIndex(self.source_b.findData(match_b.id))
        elif existing is None:
            self.source_b.setCurrentIndex(min(1, self.source_b.count() - 1))
        self.source_a.currentIndexChanged.connect(self._update_preview)
        self.source_b.currentIndexChanged.connect(self._update_preview)
        form.addRow("A", self.source_a)
        form.addRow("B", self.source_b)

        form.addRow("Formula", self.preview)
        self._update_preview()

        self.dmin = self._dspin(existing.display_min if existing else 0.0)
        self.dmax = self._dspin(existing.display_max if existing else 100.0)
        form.addRow("Display min", self.dmin)
        form.addRow("Display max", self.dmax)

        self.notes = QPlainTextEdit(existing.notes if existing else "")
        self.notes.setPlaceholderText(
            "What this combination means, why it's useful…"
        )
        self.notes.setMaximumHeight(60)
        form.addRow("Notes", self.notes)

        self.regen = QCheckBox("Regenerate firmware catalog (run gen_catalog.py)")
        self.regen.setChecked(True)
        form.addRow("", self.regen)

        buttons = QHBoxLayout()
        buttons.addStretch(1)
        cancel_btn = QPushButton("Cancel")
        cancel_btn.clicked.connect(self.reject)
        buttons.addWidget(cancel_btn)
        save_btn = QPushButton("Save")
        mark_primary(save_btn)
        save_btn.clicked.connect(self._save)
        buttons.addWidget(save_btn)
        form.addRow(buttons)

    def _dspin(self, value: float) -> QDoubleSpinBox:
        sp = QDoubleSpinBox()
        sp.setRange(-1e9, 1e9)
        sp.setDecimals(4)
        sp.setValue(value)
        return sp

    def _find_source_match(self, req: Request | None) -> Signal | None:
        """A live direct signal whose own request is identical to `req` --
        used in edit mode to preselect where a stored request came from."""
        if req is None:
            return None
        return next(
            (
                s for s in self._sources
                if s.requests
                and s.requests[0].byte1 == req.byte1
                and s.requests[0].byte2 == req.byte2
                and s.requests[0].offset == req.offset
                and s.requests[0].scale == req.scale
            ),
            None,
        )

    def _populate_source_combo(self, combo: QComboBox, fallback: Request | None) -> None:
        if fallback is not None:
            combo.addItem(
                f"(unchanged — DID {fallback.byte1:02X} {fallback.byte2:02X}, "
                "no longer matches a registry signal)",
                userData=None,
            )
        for sig in self._sources:
            unit = f" ({sig.unit})" if sig.unit != "None" else ""
            combo.addItem(f"{sig.name}{unit}", userData=sig.id)

    def _selected_source(self, combo: QComboBox) -> Signal | None:
        sid = combo.currentData()
        return next((s for s in self._sources if s.id == sid), None)

    def _resolve_request(self, combo: QComboBox, fallback: Request | None) -> Request | None:
        sig = self._selected_source(combo)
        if sig is not None:
            return sig.requests[0]
        return fallback  # the "(unchanged)" placeholder is still selected

    def _update_preview(self) -> None:
        a = self._selected_source(self.source_a)
        b = self._selected_source(self.source_b)
        op_key = self.op.currentData()
        a_name = a.name if a else ("A" if self._fallback_a is None else "(unchanged A)")
        b_name = b.name if b else ("B" if self._fallback_b is None else "(unchanged B)")
        formula = {
            "sum": f"{a_name} + {b_name}",
            "difference": f"{b_name} − {a_name}",
            "average": f"average({a_name}, {b_name})",
            "min": f"min({a_name}, {b_name})",
            "max": f"max({a_name}, {b_name})",
        }.get(op_key, "")
        self.preview.setText(formula)

    def _save(self) -> None:
        req_a = self._resolve_request(self.source_a, self._fallback_a)
        req_b = self._resolve_request(self.source_b, self._fallback_b)
        if req_a is None or req_b is None:
            QMessageBox.warning(self, "Save", "Choose two different signals.")
            return
        sid_a, sid_b = self.source_a.currentData(), self.source_b.currentData()
        if sid_a is not None and sid_a == sid_b:
            QMessageBox.warning(self, "Save", "Choose two different signals.")
            return

        status = self.status_combo.currentData()
        name = self.name.text().strip()
        if status != "wip" and not name:
            QMessageBox.warning(
                self, "Save",
                f"Give the signal a name before marking it "
                f"{STATUS_LABELS[status]}.",
            )
            return

        signal = Signal(
            id=self.existing.id if self.existing else self.catalog.next_id(),
            name=name,
            unit=self.unit.currentData(),
            cycle_time_ms=self.cycle.value(),
            transform=self.op.currentData(),
            display_min=self.dmin.value(),
            display_max=self.dmax.value(),
            requests=[dataclasses.replace(req_a), dataclasses.replace(req_b)],
            status=status,
            description=_description_text(self.description),
            notes=self.notes.toPlainText().strip(),
            samples=list(self.existing.samples) if self.existing else [],
            points=list(self.existing.points) if self.existing else [],
        )

        if status != "wip" and not _confirm_firmware_ok(self, signal, self.catalog):
            return

        if self.existing is not None:
            self.catalog.update_signal(signal)
        else:
            self.catalog.add_signal(signal)
        self.saved_signal = signal
        self.saved_path = self.catalog.save()

        if self.regen.isChecked():
            try:
                _run_gen_catalog()
                self.regenerated = True
            except (subprocess.CalledProcessError, OSError) as e:
                QMessageBox.warning(
                    self, "Regenerate",
                    f"Saved signals.json, but gen_catalog.py failed:\n\n"
                    f"{_gen_catalog_error(e)}",
                )

        self.accept()


def _open_signal_editor(
    catalog: Catalog, existing: Signal, parent=None,
    engine: MonitorEngine | None = None,
) -> QDialog:
    """Routes to the right editor for an existing registry entry: composite
    signals built by CompoundSignalDialog's generic transforms go back
    through it (so their sources/operation stay editable); everything else
    -- direct signals, and composites with a bespoke transform like
    actual_intake_pressure -- goes through SignalEditorDialog as before."""
    if existing.is_composite and existing.transform in COMPOUND_TRANSFORMS:
        return CompoundSignalDialog(catalog, existing=existing, parent=parent)
    return SignalEditorDialog(catalog, existing=existing, parent=parent, engine=engine)


def _new_field_dialog(window, parent, signal: Signal | None) -> QDialog | None:
    """An editor for a new signal reading other bytes of `signal`'s DID, or
    None (after saying why) when `signal` can't have one."""
    if signal is None:
        QMessageBox.information(parent, "New field", "Select a signal first.")
        return None
    if signal.is_composite:
        QMessageBox.information(
            parent, "New field",
            "That signal combines several requests — pick a direct one.")
        return None
    live_entry = _pick_responder(parent, signal, window.engine.snapshot())
    return SignalEditorDialog(
        window.catalog, sibling_of=signal,
        live_samples=live_entry.samples if live_entry else [],
        parent=parent, engine=window.engine,
        live_rx=live_entry.rx_id if live_entry else None,
    )


class WipTab(QWidget):
    """Every saved-but-not-yet-active signal: a holding area for DIDs you've
    started reverse-engineering but haven't confirmed a scale/offset for.
    Resume one to keep refining it, or delete it if it turned out to be a
    dead end. Nothing here reaches the firmware's compiled catalog until you
    resume an entry and set its Status to Active."""

    COLUMNS = ["DID", "ECU", "Name", "Unit", "Notes", "Samples", "Live?", "Updated"]

    def __init__(self, catalog: Catalog, window) -> None:
        super().__init__()
        self.catalog = catalog
        self.window = window
        self._entries: list[Signal] = []

        layout = QVBoxLayout(self)
        info = QLabel(
            "Signals you've started figuring out but haven't confirmed a "
            "scale/offset for yet. Nothing here affects the dashboard or "
            "firmware until you resume one and set its Status to Active."
        )
        info.setWordWrap(True)
        layout.addWidget(info)

        self.hint = QLabel(
            "No WIP signals yet — save one from the Learn tab when you're "
            "not ready to commit it to the catalog."
        )
        self.hint.setAlignment(Qt.AlignCenter)
        self.hint.setWordWrap(True)
        self.hint.setStyleSheet(f"color: {TEXT_DIM}; padding: 32px;")
        layout.addWidget(self.hint, stretch=1)

        self.table = QTableWidget(0, len(self.COLUMNS))
        self.table.setHorizontalHeaderLabels(self.COLUMNS)
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.horizontalHeader().setSectionResizeMode(4, QHeaderView.Stretch)
        self.table.doubleClicked.connect(lambda *_: self._resume_selected())
        layout.addWidget(self.table, stretch=1)

        btns = QHBoxLayout()
        btns.addStretch(1)
        del_btn = QPushButton("Delete")
        del_btn.clicked.connect(self._delete_selected)
        btns.addWidget(del_btn)
        sibling_btn = QPushButton("New Field on DID…")
        sibling_btn.setToolTip(NEW_FIELD_TIP)
        sibling_btn.clicked.connect(self._new_field)
        btns.addWidget(sibling_btn)
        resume_btn = QPushButton("Resume…")
        mark_primary(resume_btn)
        resume_btn.clicked.connect(self._resume_selected)
        btns.addWidget(resume_btn)
        layout.addLayout(btns)

        self.refresh(unknown_keys=set())

    def refresh(self, unknown_keys: set) -> None:
        entries = sorted(
            (s for s in self.catalog.signals if s.is_wip),
            key=lambda s: s.updated_at, reverse=True,
        )
        self._entries = entries

        empty = not entries
        self.hint.setVisible(empty)
        self.table.setVisible(not empty)
        if empty:
            self.table.setRowCount(0)
            return

        self.table.setRowCount(len(entries))
        for row, sig in enumerate(entries):
            did = sig.requests[0].did if sig.requests else None
            ecu = sig.requests[0].ecu if sig.requests else None
            live = did is not None and any(
                d == did and (ecu is None or ecu.rx == rx)
                for rx, d in unknown_keys)
            vals = [
                f"{did[0]:02X} {did[1]:02X}" if did else "—",
                _ecu_text(sig),
                sig.name or "(unnamed)",
                sig.unit,
                sig.notes.replace("\n", " ") or "—",
                str(len(sig.samples)),
                "● live" if live else "—",
                _fmt_age(time.time(), sig.updated_at),
            ]
            for col, text in enumerate(vals):
                item = QTableWidgetItem(text)
                if col in (0, 1, 5, 7):
                    _mono(item)
                if col == 6 and live:
                    item.setForeground(QColor(GREEN))
                self.table.setItem(row, col, item)

    def _selected_signal(self) -> Signal | None:
        row = self.table.currentRow()
        if row < 0 or row >= len(self._entries):
            return None
        return self._entries[row]

    def _resume_selected(self) -> None:
        signal = self._selected_signal()
        if signal is None:
            QMessageBox.information(self, "WIP", "Select a WIP signal first.")
            return
        if signal.is_composite and signal.transform in COMPOUND_TRANSFORMS:
            dlg = CompoundSignalDialog(self.catalog, existing=signal, parent=self)
        else:
            live_entry = (
                _pick_responder(self, signal, self.window.engine.snapshot())
                if signal.requests else None
            )
            dlg = SignalEditorDialog(
                self.catalog, existing=signal,
                live_samples=live_entry.samples if live_entry else [],
                live_ecu=live_entry.ecu if live_entry else None, parent=self,
                engine=self.window.engine,
                live_rx=live_entry.rx_id if live_entry else None,
            )
        if dlg.exec() == QDialog.Accepted:
            self.window.on_signal_saved(dlg.saved_signal, dlg.saved_path, dlg.regenerated)
            if dlg.saved_signal.in_firmware:
                self.window._select_page(0)  # graduated -- show it live

    def _new_field(self) -> None:
        dlg = _new_field_dialog(self.window, self, self._selected_signal())
        if dlg is not None and dlg.exec() == QDialog.Accepted:
            self.window.on_signal_saved(dlg.saved_signal, dlg.saved_path, dlg.regenerated)

    def _delete_selected(self) -> None:
        signal = self._selected_signal()
        if signal is None:
            QMessageBox.information(self, "WIP", "Select a WIP signal first.")
            return
        name = signal.name or f"signal {signal.id}"
        if QMessageBox.question(
            self, "Delete WIP signal", f"Delete WIP notes for “{name}”?"
        ) != QMessageBox.Yes:
            return
        self.catalog.remove_signal(signal.id)
        self.catalog.save()
        self.window.on_wip_saved()


class RegistryTab(QWidget):
    """Every signal in the registry, active and WIP alike -- the birds-eye
    view for reviewing what's actually validated. Learn (bus discovery) and
    WIP (holding-area review) stay dedicated workflows; this tab is where
    you go through the whole registry at once: filter, Set Status for a
    fast change, Edit… to change anything else, or Move Up/Down to reorder
    (see Catalog.move_signal -- only enabled with the "All" filter, since a
    filtered view isn't the registry's real, contiguous order)."""

    COLUMNS = ["ID", "Name", "Status", "Unit", "DID(s)", "ECU", "Cycle", "Notes"]

    def __init__(self, catalog: Catalog, window) -> None:
        super().__init__()
        self.catalog = catalog
        self.window = window
        self._rows: list[Signal] = []

        layout = QVBoxLayout(self)

        controls = QHBoxLayout()
        controls.addWidget(QLabel("Filter:"))
        self.filter = QComboBox()
        self.filter.addItems(["All", "Active only", "Internal only", "WIP only"])
        self.filter.currentIndexChanged.connect(self.refresh)
        controls.addWidget(self.filter)
        controls.addStretch(1)
        self.count_label = QLabel()
        controls.addWidget(self.count_label)
        # Up here rather than with the per-signal buttons below: it acts on
        # the whole registry, and that row has no width left for it.
        regen_btn = QPushButton("Regenerate Firmware Catalog")
        mark_primary(regen_btn)
        regen_btn.setToolTip(
            "Run tools/gen_catalog.py so signal_catalog.generated.inc "
            "reflects any status changes made here."
        )
        regen_btn.clicked.connect(self._regenerate)
        controls.addWidget(regen_btn)
        layout.addLayout(controls)

        self.table = QTableWidget(0, len(self.COLUMNS))
        self.table.setHorizontalHeaderLabels(self.COLUMNS)
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.table.horizontalHeader().setSectionResizeMode(7, QHeaderView.Stretch)
        self.table.doubleClicked.connect(lambda *_: self._edit_selected())
        layout.addWidget(self.table, stretch=1)

        btns = QHBoxLayout()
        new_btn = QPushButton("New Signal…")
        new_btn.clicked.connect(self._new_signal)
        btns.addWidget(new_btn)
        new_compound_btn = QPushButton("New Compound Signal…")
        new_compound_btn.setToolTip(
            "Combine two existing direct signals (e.g. a difference or "
            "average) into a new computed signal — no new firmware code "
            "needed for sum/difference/average/min/max."
        )
        new_compound_btn.clicked.connect(self._new_compound_signal)
        btns.addWidget(new_compound_btn)
        new_field_btn = QPushButton("New Field on DID…")
        new_field_btn.setToolTip(NEW_FIELD_TIP)
        new_field_btn.clicked.connect(self._new_field)
        btns.addWidget(new_field_btn)
        del_btn = QPushButton("Delete")
        del_btn.clicked.connect(self._delete_selected)
        btns.addWidget(del_btn)
        move_up_btn = QPushButton("Move Up")
        move_up_btn.setToolTip(
            "Move the selected signal earlier in the registry's own order "
            "(filter must be \"All\"). This is the order gen_catalog.py "
            "compiles active signals into, so it changes the firmware's "
            "SIGNAL_CATALOG[] array order too."
        )
        move_up_btn.clicked.connect(lambda: self._move_selected(-1))
        btns.addWidget(move_up_btn)
        move_down_btn = QPushButton("Move Down")
        move_down_btn.setToolTip("Move the selected signal later in the registry's own order.")
        move_down_btn.clicked.connect(lambda: self._move_selected(1))
        btns.addWidget(move_down_btn)
        btns.addStretch(1)
        status_btn = QPushButton("Set Status")
        status_btn.setToolTip(
            "Set the selected signal(s) to Active, Internal or WIP without "
            "opening the editor. Run Regenerate afterward to sync the "
            "firmware's compiled catalog."
        )
        status_menu = QMenu(status_btn)
        for status in ("active", "internal", "wip"):
            status_menu.addAction(
                STATUS_LABELS[status],
                lambda s=status: self._set_selected_status(s),
            )
        status_btn.setMenu(status_menu)
        btns.addWidget(status_btn)
        edit_btn = QPushButton("Edit…")
        edit_btn.clicked.connect(self._edit_selected)
        btns.addWidget(edit_btn)
        layout.addLayout(btns)

        self.refresh()

    def refresh(self) -> None:
        f = self.filter.currentText()
        signals = self.catalog.signals
        wanted = {"Active only": "active", "Internal only": "internal",
                  "WIP only": "wip"}.get(f)
        if wanted is not None:
            signals = [s for s in signals if s.status == wanted]
        self._rows = signals

        self.table.setRowCount(len(signals))
        for row, sig in enumerate(signals):
            dids = "; ".join(f"{r.byte1:02X} {r.byte2:02X}" for r in sig.requests) or "—"
            vals = [
                str(sig.id),
                sig.name or "(unnamed)",
                STATUS_LABELS.get(sig.status, sig.status),
                sig.unit,
                dids,
                _ecu_text(sig),
                str(sig.cycle_time_ms),
                sig.notes.replace("\n", " ") or "—",
            ]
            for col, text in enumerate(vals):
                item = QTableWidgetItem(text)
                if col in (0, 6):
                    item.setTextAlignment(Qt.AlignRight | Qt.AlignVCenter)
                if col in (0, 4, 5, 6):
                    _mono(item)
                if col == 2:
                    item.setForeground(QColor(_status_color(sig.status)))
                self.table.setItem(row, col, item)

        self.count_label.setText(f"{len(signals)} of {len(self.catalog.signals)} signals")

    def _selected_signals(self) -> list[Signal]:
        rows = sorted({i.row() for i in self.table.selectedItems()})
        return [self._rows[r] for r in rows if r < len(self._rows)]

    def _edit_selected(self) -> None:
        selected = self._selected_signals()
        if not selected:
            QMessageBox.information(self, "Registry", "Select a signal first.")
            return
        dlg = _open_signal_editor(self.catalog, selected[0], parent=self,
                                  engine=self.window.engine)
        if dlg.exec() == QDialog.Accepted:
            self.window.on_signal_saved(dlg.saved_signal, dlg.saved_path, dlg.regenerated)

    def _new_field(self) -> None:
        selected = self._selected_signals()
        dlg = _new_field_dialog(self.window, self, selected[0] if selected else None)
        if dlg is not None and dlg.exec() == QDialog.Accepted:
            self.window.on_signal_saved(dlg.saved_signal, dlg.saved_path, dlg.regenerated)

    def _new_signal(self) -> None:
        dlg = SignalEditorDialog(self.catalog, parent=self, engine=self.window.engine)
        if dlg.exec() == QDialog.Accepted:
            self.window.on_signal_saved(dlg.saved_signal, dlg.saved_path, dlg.regenerated)

    def _new_compound_signal(self) -> None:
        dlg = CompoundSignalDialog(self.catalog, parent=self)
        if dlg.exec() == QDialog.Accepted:
            self.window.on_signal_saved(dlg.saved_signal, dlg.saved_path, dlg.regenerated)

    def _set_selected_status(self, status: str) -> None:
        selected = self._selected_signals()
        if not selected:
            QMessageBox.information(self, "Registry", "Select at least one signal first.")
            return
        if status != "wip":
            for sig in selected:
                probe = dataclasses.replace(sig, status=status)
                if not _confirm_firmware_ok(self, probe, self.catalog):
                    return
        for sig in selected:
            sig.status = status
            self.catalog.update_signal(sig)
        self.catalog.save()
        self.refresh()
        self.window.on_registry_changed()

    def _delete_selected(self) -> None:
        selected = self._selected_signals()
        if not selected:
            QMessageBox.information(self, "Registry", "Select a signal first.")
            return
        names = ", ".join(s.name or f"signal {s.id}" for s in selected)
        msg = f"Delete {len(selected)} signal(s): {names}?"
        if any(s.in_firmware for s in selected):
            msg += (
                "\n\nThis includes signal(s) currently compiled into the "
                "firmware."
            )
        if QMessageBox.question(self, "Delete signal(s)", msg) != QMessageBox.Yes:
            return
        for sig in selected:
            self.catalog.remove_signal(sig.id)
        self.catalog.save()
        self.refresh()
        self.window.on_registry_changed()

    def _move_selected(self, delta: int) -> None:
        if self.filter.currentText() != "All":
            QMessageBox.information(
                self, "Reorder",
                "Switch the filter to \"All\" first — reordering works on "
                "the registry's real order, which a filtered view doesn't "
                "show contiguously.",
            )
            return
        selected = self._selected_signals()
        if len(selected) != 1:
            QMessageBox.information(self, "Reorder", "Select exactly one signal to move.")
            return
        sig = selected[0]
        if not self.catalog.move_signal(sig.id, delta):
            return  # already at that end -- silently a no-op, same as the buttons being disabled
        self.catalog.save()
        self.refresh()
        self._select_signal_id(sig.id)
        self.window.on_registry_changed()

    def _select_signal_id(self, signal_id: int) -> None:
        for row, sig in enumerate(self._rows):
            if sig.id == signal_id:
                self.table.selectRow(row)
                self.table.scrollToItem(self.table.item(row, 0))
                return

    def _regenerate(self) -> None:
        try:
            _run_gen_catalog()
        except (subprocess.CalledProcessError, OSError) as e:
            QMessageBox.warning(self, "Regenerate",
                                f"gen_catalog.py failed:\n\n{_gen_catalog_error(e)}")
            return
        QMessageBox.information(self, "Regenerate", "Firmware catalog regenerated.")


class MainWindow(QMainWindow):
    PAGES = ["Signals", "Traffic", "Registry", "Learn", "WIP", "Fake Car"]

    def __init__(self, port: str | None, demo: bool, listen_only: bool) -> None:
        super().__init__()
        self.setWindowTitle("MQB Monitor — UDS CAN decoder")
        self.resize(1080, 680)

        self.catalog = Catalog.load()
        self.engine = MonitorEngine(self.catalog)
        self.settings = QSettings("MQBMonitor", "monitor")
        self.source = None
        self._recorder: CaptureWriter | None = None
        self._flash: tuple[str, float] | None = None  # (status text, until)
        # Set once made-up traffic (demo, the simulated dongle, the fake
        # car's own answers) is in the engine's state: _tick's ECU auto-fill
        # must never learn from it.
        self._synthetic = False
        self.fake = FakeCar(self._fake_send)
        self._fake_on = False
        self._connect_error: str | None = None
        self.listen_only = listen_only
        self._preselect_port = port
        self._preselect_demo = demo

        root = QWidget()
        root.setObjectName("Root")
        root_layout = QVBoxLayout(root)
        root_layout.setContentsMargins(0, 0, 0, 0)
        root_layout.setSpacing(0)
        root_layout.addWidget(self._build_topbar())

        body = QWidget()
        body_layout = QHBoxLayout(body)
        body_layout.setContentsMargins(0, 0, 0, 0)
        body_layout.setSpacing(0)

        self.sidebar = self._build_sidebar()
        body_layout.addWidget(self.sidebar)

        self.stack = QStackedWidget()
        self.signals_tab = SignalsTab(self.catalog, self.settings)
        self.traffic_tab = TrafficTab()
        self.registry_tab = RegistryTab(self.catalog, self)
        self.learn_tab = LearnTab(self.catalog, self.engine, self)
        self.wip_tab = WipTab(self.catalog, self)
        self.fake_tab = FakeCarTab(self.catalog, self)
        for page in (
            self.signals_tab, self.traffic_tab, self.registry_tab,
            self.learn_tab, self.wip_tab, self.fake_tab,
        ):
            self.stack.addWidget(page)
        body_layout.addWidget(self.stack, stretch=1)

        root_layout.addWidget(body, stretch=1)
        root_layout.addWidget(self._build_statusbar())
        self.setCentralWidget(root)

        self.sidebar.currentRowChanged.connect(self.stack.setCurrentIndex)
        self.sidebar.setCurrentRow(0)

        self.timer = QTimer(self)
        self.timer.setInterval(66)  # ~15 Hz
        self.timer.timeout.connect(self._tick)
        self.timer.start()

        if demo:
            self._connect()

    def _select_page(self, index: int) -> None:
        self.sidebar.setCurrentRow(index)  # drives self.stack via the signal

    def _build_sidebar(self) -> QListWidget:
        sidebar = QListWidget()
        sidebar.setObjectName("Sidebar")
        sidebar.setFixedWidth(160)
        sidebar.setFrameShape(QFrame.NoFrame)
        for name in self.PAGES:
            sidebar.addItem(QListWidgetItem(name))
        return sidebar

    def _build_topbar(self) -> QFrame:
        bar = QFrame()
        bar.setObjectName("TopBar")
        layout = QHBoxLayout(bar)
        layout.setContentsMargins(16, 10, 16, 10)
        layout.setSpacing(10)

        title_box = QVBoxLayout()
        title_box.setSpacing(0)
        title = QLabel("MQB Monitor")
        title.setObjectName("AppTitle")
        subtitle = QLabel("UDS CAN decoder")
        subtitle.setObjectName("AppSubtitle")
        title_box.addWidget(title)
        title_box.addWidget(subtitle)
        layout.addLayout(title_box)

        sep = QFrame()
        sep.setFrameShape(QFrame.VLine)
        sep.setStyleSheet(f"color: {TEXT_DIM};")
        layout.addSpacing(8)
        layout.addWidget(sep)
        layout.addSpacing(8)

        source_label = QLabel("Source")
        source_label.setObjectName("FieldLabel")
        layout.addWidget(source_label)
        self.source_combo = QComboBox()
        self.source_combo.setMinimumWidth(220)
        # Sized by a fixed character count, not the longest entry, so a long
        # port description can't widen the window; the popup still widens
        # to fit (see _populate_ports).
        self.source_combo.setSizeAdjustPolicy(
            QComboBox.AdjustToMinimumContentsLengthWithIcon)
        self.source_combo.setMinimumContentsLength(24)
        self._populate_ports()
        layout.addWidget(self.source_combo)

        refresh = QPushButton("Refresh")
        refresh.setToolTip("Refresh serial ports")
        refresh.clicked.connect(self._populate_ports)
        layout.addWidget(refresh)

        baud_label = QLabel("Baud")
        baud_label.setObjectName("FieldLabel")
        layout.addWidget(baud_label)
        self.baud_combo = QComboBox()
        self.baud_combo.setToolTip(
            "CAN bus bitrate the Teensy sniffer opens at -- must match "
            "whatever's actually on the bus. 500 kbit/s is the MQB "
            "powertrain rate; the others are for bench testing against a "
            "different bus."
        )
        for rate in sorted(SLCAN_BAUD_INDEX):
            label = f"{rate // 1000} kbit/s"
            if rate == DEFAULT_CAN_BAUD:
                label += " (MQB)"
            self.baud_combo.addItem(label, userData=rate)
        saved_baud = int(self.settings.value("can_baud", DEFAULT_CAN_BAUD))
        idx = self.baud_combo.findData(saved_baud)
        self.baud_combo.setCurrentIndex(idx if idx >= 0 else self.baud_combo.findData(DEFAULT_CAN_BAUD))
        self.baud_combo.currentIndexChanged.connect(
            lambda: self.settings.setValue("can_baud", self.baud_combo.currentData())
        )
        layout.addWidget(self.baud_combo)

        self.connect_btn = QPushButton("Connect")
        mark_primary(self.connect_btn)
        self.connect_btn.setMinimumWidth(100)  # fits "Disconnect" too -- no jump on toggle
        self.connect_btn.clicked.connect(self._toggle_connect)
        layout.addWidget(self.connect_btn)

        self.record_btn = QPushButton("● Rec")
        self.record_btn.setCheckable(True)
        self.record_btn.setEnabled(False)
        self.record_btn.setToolTip(
            "Write every raw frame from now on to tools/monitor/captures/ "
            "(replay it later from Source → Replay capture…)."
        )
        self.record_btn.toggled.connect(self._toggle_record)
        layout.addWidget(self.record_btn)

        self.listen_check = QCheckBox("Listen-only")
        self.listen_check.setChecked(self.listen_only)
        self.listen_check.setToolTip(
            "Passive: never ACK or transmit on the bus (safe to sit between "
            "the OBDEleven and the car). Uncheck only for active probing."
        )
        layout.addWidget(self.listen_check)

        self.metric_check = QCheckBox("Metric")
        self.metric_check.setChecked(True)
        layout.addWidget(self.metric_check)

        layout.addStretch(1)
        return bar

    def _build_statusbar(self) -> QFrame:
        """Connection state and traffic counts, along the bottom: the top
        bar has no width to spare for them at the default window size."""
        bar = QFrame()
        bar.setObjectName("StatusBar")
        layout = QHBoxLayout(bar)
        layout.setContentsMargins(16, 4, 16, 4)
        layout.setSpacing(8)
        self.status_dot = QLabel("●")
        self.status_dot.setObjectName("StatusDot")
        self.status_dot.setStyleSheet(f"color: {TEXT_DIM};")
        layout.addWidget(self.status_dot)
        self.status = QLabel("Disconnected")
        self.status.setObjectName("StatsLabel")
        self.status.setFont(MONO_FONT)
        layout.addWidget(self.status)
        layout.addStretch(1)
        return bar

    def _populate_ports(self) -> None:
        current = self.source_combo.currentData() if self.source_combo.count() else None
        self.source_combo.clear()
        self.source_combo.addItem("Demo (synthesized traffic)", userData="__demo__")
        self.source_combo.addItem("Replay capture…", userData="__file__")
        self.source_combo.addItem("Simulated dongle (fake car test)", userData="__sim__")
        for dev, desc in available_ports():
            self.source_combo.addItem(f"{dev} — {desc}", userData=dev)
        view = self.source_combo.view()
        view.setMinimumWidth(view.sizeHintForColumn(0) + 24)
        if self._preselect_demo:
            self.source_combo.setCurrentIndex(0)
        else:
            # A port picked earlier wins only while it's still plugged in;
            # otherwise re-guess, so Refresh after plugging the dash in
            # lands on it rather than on whatever was showing.
            listed = self.source_combo.findData(current) >= 0
            kept = current if listed and current not in ("__demo__", "__file__", "__sim__") else None
            target = self._preselect_port or kept or guess_teensy_port()
            if target:
                idx = self.source_combo.findData(target)
                if idx >= 0:
                    self.source_combo.setCurrentIndex(idx)

    def _toggle_connect(self) -> None:
        if self.source is None:
            self._connect()
        else:
            self._disconnect()

    def _connect(self) -> None:
        self._connect_error = None
        choice = self.source_combo.currentData()
        if choice == "__file__":
            self._start_replay()
            return
        try:
            if choice == "__demo__":
                self.source = DemoSource(self.catalog, self._on_frame)
                self._synthetic = True
            elif choice == "__sim__":
                self.source = SimDongleSource(self._on_frame, self.fake.targets)
                self._synthetic = True
            else:
                self.source = SlcanSerialSource(
                    choice, self._on_frame,
                    listen_only=self.listen_check.isChecked(),
                    can_baud=self.baud_combo.currentData(),
                )
            self.source.start()
        except Exception as e:  # noqa: BLE001 -- surface any connect failure
            self.source = None
            QMessageBox.critical(self, "Connect failed", str(e))
            return
        self._set_connected(True)

    def _set_connected(self, connected: bool) -> None:
        self.connect_btn.setText("Disconnect" if connected else "Connect")
        self.source_combo.setEnabled(not connected)
        self.listen_check.setEnabled(not connected)
        self.baud_combo.setEnabled(not connected)
        # Recording a replay would only copy the file.
        self.record_btn.setEnabled(
            connected and not isinstance(self.source, FileSource))

    def _start_replay(self) -> None:
        start_dir = CAPTURE_DIR if CAPTURE_DIR.is_dir() else Path.home()
        path, _ = QFileDialog.getOpenFileName(
            self, "Replay Capture", str(start_dir),
            "Captures and raw traffic logs (*.csv)")
        if not path:
            return
        speeds = ["Real time", "All at once"]
        speed, ok = QInputDialog.getItem(
            self, "Replay Capture",
            "Replaying clears the traffic seen so far. Play it back:",
            speeds, 0, False)
        if not ok:
            return
        try:
            src = FileSource(path, self._on_frame, realtime=speed == speeds[0])
        except (OSError, ValueError) as e:
            QMessageBox.critical(self, "Replay failed", str(e))
            return
        self.engine.reset(started_at=src.base)
        self.traffic_tab._clear()
        # The replay is the whole session now; only a demo capture taints it.
        self._synthetic = src.info.is_demo
        self.source = src
        src.start()
        self._set_connected(True)

    def _disconnect(self) -> None:
        self.record_btn.setChecked(False)  # stops a recording
        self.fake_tab.stopped()  # the fake car needs a bus
        if self.source is not None:
            self.source.stop()
            self.source = None
        self._set_connected(False)

    def _on_frame(self, frame) -> None:
        """Every frame from the source thread: into the engine, and into
        the capture file while recording."""
        self.engine.feed(frame)
        recorder = self._recorder
        if recorder is not None:
            recorder.write(frame, frame_note(frame))
        if self._fake_on:
            self.fake.on_frame(frame)

    def _fake_send(self, frame) -> None:
        """The fake car's answers: onto the bus, and into the engine and
        any recording like everything else seen on it."""
        src = self.source
        if src is not None and getattr(src, "can_send", False):
            src.send(frame)
        self.engine.feed(frame)
        recorder = self._recorder
        if recorder is not None:
            recorder.write(frame, frame_note(frame))

    def start_fake_car(self) -> bool:
        """Lets the fake car answer, if the source can transmit. False (after
        saying why) if not."""
        src = self.source
        if not self.fake.modules():
            QMessageBox.information(
                self, "Start Fake Car",
                "There's nothing to answer with yet: load a recording of "
                "the dongle talking to the car (Load Recording…), or add a "
                "signal to fake.")
            return False
        if isinstance(src, SimDongleSource):
            pass
        elif isinstance(src, SlcanSerialSource) and src.can_send:
            if QMessageBox.question(
                self, "Start Fake Car",
                "The fake car will answer diagnostic requests on this bus as "
                "if it were the car's modules.\n\nOnly do this with the dash "
                "or sniffer on a bench OBD socket — never plugged into the "
                "car, where the real modules answer too.\n\nStart it?",
            ) != QMessageBox.Yes:
                return False
        elif isinstance(src, SlcanSerialSource) and src.listen_only:
            QMessageBox.information(
                self, "Start Fake Car",
                "This connection is listen-only, so the fake car can't "
                "answer.\n\nDisconnect, untick Listen-only, and Connect "
                "again.")
            return False
        else:
            QMessageBox.information(
                self, "Start Fake Car",
                "The fake car needs a bus it can transmit on. Either:\n\n"
                "• Source → Simulated dongle, to try it with no hardware, or\n"
                "• the Teensy dash or standalone sniffer on a bench OBD "
                "socket, connected with Listen-only unchecked.")
            return False
        self._fake_on = True
        self._synthetic = True
        self.fake.start_background()
        return True

    def stop_fake_car(self) -> None:
        self._fake_on = False
        self.fake.stop_background()

    def _source_name(self) -> str:
        if isinstance(self.source, DemoSource):
            return "demo"
        if isinstance(self.source, SimDongleSource):
            return "simulated"
        if isinstance(self.source, SlcanSerialSource):
            return f"serial:{self.source_combo.currentData()}"
        return "unknown"

    def _toggle_record(self, on: bool) -> None:
        if on:
            source = self._source_name()
            try:
                self._recorder = CaptureWriter(new_capture_path(source), source)
            except OSError as e:
                QMessageBox.warning(self, "Record", f"Couldn't start a capture:\n{e}")
                self.record_btn.setChecked(False)
                return
            self.record_btn.setToolTip(str(self._recorder.path))
            self.record_btn.setStyleSheet(f"color: {RED};")
            return
        recorder, self._recorder = self._recorder, None
        self.record_btn.setText("● Rec")
        self.record_btn.setStyleSheet("")
        if recorder is not None:
            recorder.close()
            self._flash = (f"Saved {recorder.path.name}", time.monotonic() + 6)

    def on_signal_saved(self, signal, path, regenerated: bool) -> None:
        """Called whenever SignalEditorDialog commits a save (from Learn,
        WIP-resume, or the Registry tab). Refreshes every view that depends
        on the registry. Deliberately doesn't navigate or pop up a message --
        callers decide navigation themselves (Learn/WIP want to jump to
        where the signal landed; Registry wants to stay put for batch
        review), and the tables updating live is confirmation enough."""
        self.engine.refresh_catalog()
        self.signals_tab.refresh_catalog()
        if signal is not None and signal.in_firmware:
            self.signals_tab.add_watch(signal.id)
        self.registry_tab.refresh()
        self.wip_tab.refresh(set(self.engine.snapshot().unknown.keys()))
        self.fake_tab.refresh_signals()

    def on_wip_saved(self) -> None:
        """WIP tab's own Delete action, which mutates the catalog directly
        without going through SignalEditorDialog."""
        self.engine.refresh_catalog()
        self.registry_tab.refresh()
        self.wip_tab.refresh(set(self.engine.snapshot().unknown.keys()))
        self.fake_tab.refresh_signals()

    def on_registry_changed(self) -> None:
        """Registry tab's Toggle Status / Delete, which also mutate the
        catalog directly."""
        self.engine.refresh_catalog()
        self.signals_tab.refresh_catalog()
        self.learn_tab.update_view(self.engine.snapshot())
        self.wip_tab.refresh(set(self.engine.snapshot().unknown.keys()))
        self.fake_tab.refresh_signals()

    def _tick(self) -> None:
        snap = self.engine.snapshot()
        # A WIP signal whose module wasn't known learns it from real traffic
        # -- never once demo traffic, which answers everything as the engine
        # ECU, is in the engine's state.
        if not self._synthetic and fill_wip_ecus(self.catalog, snap.unknown):
            self.catalog.save()
            self.registry_tab.refresh()
            self.fake_tab.refresh_signals()
        is_metric = self.metric_check.isChecked()
        self.signals_tab.update_view(snap, is_metric)
        self.traffic_tab.update_view(snap, snap.started_at)
        self.learn_tab.update_view(snap)
        self.wip_tab.refresh(set(snap.unknown.keys()))
        self.fake_tab.update_view()

        rate = snap.total_frames / max(1e-3, time.monotonic() - snap.started_at)
        if (
            isinstance(self.source, SlcanSerialSource)
            and self.source.state in SlcanSerialSource.FAILED_STATES
        ):
            # The dash said no (or nothing) -- drop the port and say why
            # until the next Connect.
            self._connect_error = self.source.state
            self._disconnect()
        if self._recorder is not None:
            self.record_btn.setText(f"■ {self._recorder.count:,}")
        if isinstance(self.source, DemoSource):
            conn, dot_color = "Demo", ACCENT
        elif isinstance(self.source, SimDongleSource):
            conn, dot_color = "Sim dongle", ACCENT
        elif isinstance(self.source, FileSource):
            src = self.source
            conn = ("Replay done" if src.done
                    else f"Replay {100 * src.sent // max(src.total, 1)}%")
            dot_color = ACCENT
        elif isinstance(self.source, SlcanSerialSource):
            conn = self.source.state
            dot_color = GREEN if conn == SlcanSerialSource.STREAMING else ACCENT
        elif self._connect_error:
            conn, dot_color = self._connect_error, TEXT_DIM
        else:
            conn, dot_color = "Disconnected", TEXT_DIM
        self.status_dot.setStyleSheet(f"color: {dot_color};")
        if self._flash and time.monotonic() < self._flash[1]:
            self.status.setText(self._flash[0])
            return
        self.status.setText(
            f"{conn} · {snap.total_frames} frames · {rate:.0f}/s · "
            f"{len(snap.unknown)} unknown"
        )

    def closeEvent(self, event) -> None:  # noqa: N802 -- Qt override
        self._disconnect()
        super().closeEvent(event)


def place_on_screen(win: QMainWindow) -> None:
    """Shrink the window to the screen under the cursor if it doesn't fit
    there, then center it on that screen."""
    screen = QGuiApplication.screenAt(QCursor.pos()) or QGuiApplication.primaryScreen()
    avail = screen.availableGeometry()
    win.resize(min(win.width(), avail.width()), min(win.height(), avail.height()))
    win.move(avail.center() - win.rect().center())


def run_gui(port: str | None, demo: bool, listen_only: bool) -> int:
    app = QApplication.instance() or QApplication([])
    apply_theme(app)
    win = MainWindow(port=port, demo=demo, listen_only=listen_only)
    place_on_screen(win)
    win.show()
    return app.exec()
