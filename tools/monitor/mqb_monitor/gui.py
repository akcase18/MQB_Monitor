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
                the compiled catalog) or WIP (offset/scale not confirmed
                yet) via SignalEditorDialog's Status combo.
  * WIP      -- every WIP-status signal, for resuming (keep refining) or
                deleting. Nothing here reaches the compiled catalog until
                its status is flipped to Active.

Registry/Learn/WIP all read and write the same two-stage registry (see
catalog.py's module docstring) -- there is no separate WIP file anymore.
"""
from __future__ import annotations

import csv
import subprocess
import time
from pathlib import Path

from PySide6.QtCore import Qt, QSettings, QTimer
from PySide6.QtGui import QColor
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
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
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
    COMPOUND_TRANSFORMS,
    REPO_ROOT,
    Catalog,
    Request,
    Signal,
    decode_raw_payload,
    to_imperial,
    unit_decimals,
    unit_label,
)
from .session import MonitorEngine, Snapshot
from .sources import (
    DEFAULT_CAN_BAUD,
    SLCAN_BAUD_INDEX,
    DemoSource,
    SlcanSerialSource,
    available_ports,
    guess_teensy_port,
)
from .theme import (
    ACCENT,
    AMBER,
    GREEN,
    MONO_FONT,
    TEXT_DIM,
    apply_theme,
    mark_primary,
)

MAX_SAMPLES = 20  # mirrors session.MAX_UNKNOWN_SAMPLES / wip.MAX_SAMPLES (retired)

UNIT_TYPES = [
    "None", "Degrees", "Percent", "Pressure",
    "RPM", "Speed", "Temp", "Torque", "Volume",
]

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


COLOR_WIP_ROW = QColor(76, 141, 255, 26)  # faint accent tint: "already has WIP notes"


class LearnTab(QWidget):
    COLUMNS = ["DID", "Count", "Last payload", "as u8", "as i16", "Age", "Status"]

    def __init__(self, catalog: Catalog, engine: MonitorEngine, window) -> None:
        super().__init__()
        self.catalog = catalog
        self.engine = engine
        self.window = window
        self._dids: list[tuple[int, int]] = []

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
        self.table.horizontalHeader().setSectionResizeMode(2, QHeaderView.Stretch)
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
        unknown = sorted(snap.unknown.values(), key=lambda u: u.did)
        self._dids = [u.did for u in unknown]
        self.table.setRowCount(len(unknown))
        for row, u in enumerate(unknown):
            last = u.samples[-1] if u.samples else b""
            raw = decode_raw_payload(last) if last else 0
            i16 = int(raw)
            # Any status here is WIP -- an active-status match wouldn't show
            # up as "unknown" at all (Catalog._reindex is active-only).
            is_wip = bool(self.catalog.find_by_did(u.did))
            vals = [
                f"{u.did[0]:02X} {u.did[1]:02X}",
                str(u.count),
                last.hex(" ") or "—",
                str(last[0]) if last else "—",
                str(i16),
                _fmt_age(now, u.last_seen),
                "WIP saved" if is_wip else "—",
            ]
            for col, text in enumerate(vals):
                item = QTableWidgetItem(text)
                if col in (1, 3, 4):
                    item.setTextAlignment(Qt.AlignRight | Qt.AlignVCenter)
                if col != len(vals) - 1:  # every column but Status is a number/hex
                    _mono(item)
                item.setBackground(COLOR_WIP_ROW if is_wip else COLOR_NONE)
                self.table.setItem(row, col, item)

    def _add_selected(self) -> None:
        row = self.table.currentRow()
        if row < 0 or row >= len(self._dids):
            QMessageBox.information(self, "Learn", "Select an unknown DID first.")
            return
        did = self._dids[row]
        entry = self.engine.snapshot().unknown.get(did)
        samples = list(entry.samples) if entry else []
        matches = self.catalog.find_by_did(did)
        existing = matches[0] if matches else None
        dlg = SignalEditorDialog(
            self.catalog, existing=existing, did=None if existing else did,
            live_samples=samples, parent=self,
        )
        if dlg.exec() == QDialog.Accepted:
            self.window.on_signal_saved(dlg.saved_signal, dlg.saved_path, dlg.regenerated)
            # Jump to where it landed: Signals if newly active, WIP if still WIP.
            self.window._select_page(0 if dlg.saved_signal.status == "active" else 4)


class SignalEditorDialog(QDialog):
    """The single editing surface for a registry entry (core/vehicle/data/
    signals.json), in one of three modes:

      * Discover -- `did` given, no `existing`: a freshly-seen unknown DID
        from the Learn tab. Defaults to WIP status.
      * Create -- neither `did` nor `existing`: a signal you already know
        about (e.g. from documentation) that hasn't been seen on the bus
        yet. Shows DID byte1/byte2 entry fields, hidden in the other modes.
      * Edit -- `existing` given: review/refine any registry entry (active
        or WIP) from the Registry or WIP tab. Saving replaces it in place
        via Catalog.update_signal, preserving its id and list position.

    Status (Active/WIP) is one combo box, not a button choice -- this dialog
    *is* the two-stage toggle, in whichever mode it's opened."""

    def __init__(
        self,
        catalog: Catalog,
        existing: Signal | None = None,
        did: tuple[int, int] | None = None,
        live_samples: list[bytes] | None = None,
        parent=None,
    ) -> None:
        super().__init__(parent)
        self.catalog = catalog
        self.existing = existing
        self.saved_signal: Signal | None = None
        self.saved_path: Path | None = None
        self.regenerated = False

        if existing is not None:
            self._composite = existing.is_composite
            first_req = existing.requests[0] if existing.requests else None
            self._did = first_req.did if first_req else None
            self._creating = False
        elif did is not None:
            self._composite = False
            self._did = did
            self._creating = False
        else:
            self._composite = False
            self._did = None  # entered by the user in the DID fields below
            self._creating = True

        # Merge stored WIP samples with any live ones seen right now.
        stored = [bytes.fromhex(h) for h in (existing.samples if existing else [])]
        merged = stored + list(live_samples or [])
        self._samples = merged[-MAX_SAMPLES:]
        last = self._samples[-1] if self._samples else b""
        self._last_raw = decode_raw_payload(last) if last else 0.0

        title_did = f"{self._did[0]:02X} {self._did[1]:02X}" if self._did else None
        if existing is not None:
            self.setWindowTitle(f"Edit {existing.name or ('DID ' + title_did if title_did else 'signal')}")
        elif did is not None:
            self.setWindowTitle(f"Learn DID {title_did}")
        else:
            self.setWindowTitle("New Signal")
        self.setMinimumWidth(480)

        form = QFormLayout(self)

        header_bits = []
        if title_did:
            header_bits.append(f"<b>DID {title_did}</b>")
        if self._samples:
            header_bits.append(
                f"{len(self._samples)} sample(s), last payload "
                f"[{last.hex(' ')}], decoded raw = {self._last_raw:.0f}"
            )
        if header_bits:
            form.addRow(QLabel(" — ".join(header_bits)))

        self.status_combo = QComboBox()
        self.status_combo.addItems(["WIP", "Active"])
        self.status_combo.setCurrentText(
            "Active" if (existing and existing.status == "active") else "WIP"
        )
        form.addRow("Status", self.status_combo)

        self.name = QLineEdit(existing.name if existing else "")
        self.name.setPlaceholderText("e.g. Oil Pressure (optional while WIP)")
        form.addRow("Name", self.name)

        self.unit = QComboBox()
        self.unit.addItems(UNIT_TYPES)
        if existing and existing.unit in UNIT_TYPES:
            self.unit.setCurrentText(existing.unit)
        form.addRow("Unit", self.unit)

        self.cycle = QSpinBox()
        self.cycle.setRange(10, 60000)
        self.cycle.setValue(existing.cycle_time_ms if existing else 100)
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

        existing_req = existing.requests[0] if (existing and existing.requests) else None

        # Two-point calibration: raw -> true value solves scale & offset.
        self.raw1 = self._dspin(self._last_raw)
        self.val1 = self._dspin(0.0)
        self.raw2 = self._dspin(self._last_raw + 100)
        self.val2 = self._dspin(1.0)
        solve = QPushButton("Solve scale/offset")
        solve.clicked.connect(self._solve)
        form.addRow("Point 1: raw", self._pair(self.raw1, "→ value", self.val1))
        form.addRow("Point 2: raw", self._pair(self.raw2, "→ value", self.val2))
        form.addRow("", solve)

        self.offset = self._dspin(existing_req.offset if existing_req else 0.0)
        self.scale = self._dspin(existing_req.scale if existing_req else 1.0)
        self.scale.setDecimals(6)
        self.offset.setDecimals(4)
        self.scale.valueChanged.connect(self._update_preview)
        self.offset.valueChanged.connect(self._update_preview)
        form.addRow("Offset", self.offset)
        form.addRow("Scale", self.scale)

        if self._composite:
            for w in (self.raw1, self.val1, self.raw2, self.val2, solve, self.offset, self.scale):
                w.setEnabled(False)
            hint = QLabel(
                "This signal combines multiple UDS requests using a "
                "hand-written transform (advanced) — edit its requests[] "
                "directly in signals.json."
            )
        else:
            hint = QLabel("Not confirmed yet? Leave Status set to WIP above.")
        hint.setWordWrap(True)
        hint.setStyleSheet(f"color: {TEXT_DIM}; font-size: 11px;")
        form.addRow("", hint)

        self.dmin = self._dspin(existing.display_min if existing else 0.0)
        self.dmax = self._dspin(existing.display_max if existing else 100.0)
        form.addRow("Display min", self.dmin)
        form.addRow("Display max", self.dmax)

        self.preview = QLabel()
        form.addRow("Preview", self.preview)
        self._update_preview()

        self.notes = QPlainTextEdit(existing.notes if existing else "")
        self.notes.setPlaceholderText(
            "What you've tried, what still needs verifying, related "
            "signals, where you saw the real value to compare against…"
        )
        self.notes.setMaximumHeight(70)
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

    def _pair(self, a, label, b) -> QWidget:
        w = QWidget()
        h = QHBoxLayout(w)
        h.setContentsMargins(0, 0, 0, 0)
        h.addWidget(a)
        h.addWidget(QLabel(label))
        h.addWidget(b)
        return w

    def _solve(self) -> None:
        # value = (raw + offset) * scale  =>  value = scale*raw + scale*offset
        r1, v1 = self.raw1.value(), self.val1.value()
        r2, v2 = self.raw2.value(), self.val2.value()
        if r1 == r2:
            QMessageBox.warning(self, "Solve", "The two raw values must differ.")
            return
        a = (v2 - v1) / (r2 - r1)  # = scale
        if a == 0:
            QMessageBox.warning(self, "Solve", "Values must differ (scale = 0).")
            return
        b = v1 - a * r1  # = scale * offset
        self.scale.setValue(a)
        self.offset.setValue(b / a)
        self._update_preview()

    def _update_preview(self) -> None:
        decoded = (self._last_raw + self.offset.value()) * self.scale.value()
        label = unit_label(self.unit.currentText(), True)
        self.preview.setText(f"last sample decodes to {decoded:.3f} {label}")

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
        status = "active" if self.status_combo.currentText() == "Active" else "wip"
        name = self.name.text().strip()
        if status == "active" and not name:
            QMessageBox.warning(
                self, "Save", "Give the signal a name before marking it Active."
            )
            return

        if self._composite:
            requests = list(self.existing.requests)  # untouched -- see hint
        else:
            did = self._resolve_did()
            if did is None:
                QMessageBox.warning(self, "Save", "Enter a valid DID (two hex bytes).")
                return
            requests = [
                Request(did[0], did[1], self.offset.value(), self.scale.value())
            ]

        signal = Signal(
            id=self.existing.id if self.existing else self.catalog.next_id(),
            name=name,
            unit=self.unit.currentText(),
            cycle_time_ms=self.cycle.value(),
            transform=self.existing.transform if self.existing else "default",
            display_min=self.dmin.value(),
            display_max=self.dmax.value(),
            requests=requests,
            status=status,
            notes=self.notes.toPlainText().strip(),
            samples=[s.hex() for s in self._samples],
        )

        if self.existing is not None:
            self.catalog.update_signal(signal)
        else:
            self.catalog.add_signal(signal)
        self.saved_signal = signal
        self.saved_path = self.catalog.save()

        if self.regen.isChecked():
            try:
                subprocess.run(
                    ["python3", str(REPO_ROOT / "tools/gen_catalog.py")],
                    check=True, capture_output=True, text=True,
                )
                self.regenerated = True
            except (subprocess.CalledProcessError, OSError) as e:
                QMessageBox.warning(
                    self, "Regenerate",
                    f"Saved signals.json, but gen_catalog.py failed:\n{e}",
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

        self.status_combo = QComboBox()
        self.status_combo.addItems(["WIP", "Active"])
        self.status_combo.setCurrentText(
            "Active" if (existing and existing.status == "active") else "WIP"
        )
        form.addRow("Status", self.status_combo)

        self.name = QLineEdit(existing.name if existing else "")
        self.name.setPlaceholderText("e.g. Boost Above Ambient")
        form.addRow("Name", self.name)

        self.unit = QComboBox()
        self.unit.addItems(UNIT_TYPES)
        if existing and existing.unit in UNIT_TYPES:
            self.unit.setCurrentText(existing.unit)
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

        status = "active" if self.status_combo.currentText() == "Active" else "wip"
        name = self.name.text().strip()
        if status == "active" and not name:
            QMessageBox.warning(
                self, "Save", "Give the signal a name before marking it Active."
            )
            return

        signal = Signal(
            id=self.existing.id if self.existing else self.catalog.next_id(),
            name=name,
            unit=self.unit.currentText(),
            cycle_time_ms=self.cycle.value(),
            transform=self.op.currentData(),
            display_min=self.dmin.value(),
            display_max=self.dmax.value(),
            requests=[
                Request(req_a.byte1, req_a.byte2, req_a.offset, req_a.scale),
                Request(req_b.byte1, req_b.byte2, req_b.offset, req_b.scale),
            ],
            status=status,
            notes=self.notes.toPlainText().strip(),
            samples=list(self.existing.samples) if self.existing else [],
        )

        if self.existing is not None:
            self.catalog.update_signal(signal)
        else:
            self.catalog.add_signal(signal)
        self.saved_signal = signal
        self.saved_path = self.catalog.save()

        if self.regen.isChecked():
            try:
                subprocess.run(
                    ["python3", str(REPO_ROOT / "tools/gen_catalog.py")],
                    check=True, capture_output=True, text=True,
                )
                self.regenerated = True
            except (subprocess.CalledProcessError, OSError) as e:
                QMessageBox.warning(
                    self, "Regenerate",
                    f"Saved signals.json, but gen_catalog.py failed:\n{e}",
                )

        self.accept()


def _open_signal_editor(catalog: Catalog, existing: Signal, parent=None) -> QDialog:
    """Routes to the right editor for an existing registry entry: composite
    signals built by CompoundSignalDialog's generic transforms go back
    through it (so their sources/operation stay editable); everything else
    -- direct signals, and composites with a bespoke transform like
    actual_intake_pressure -- goes through SignalEditorDialog as before."""
    if existing.is_composite and existing.transform in COMPOUND_TRANSFORMS:
        return CompoundSignalDialog(catalog, existing=existing, parent=parent)
    return SignalEditorDialog(catalog, existing=existing, parent=parent)


class WipTab(QWidget):
    """Every saved-but-not-yet-active signal: a holding area for DIDs you've
    started reverse-engineering but haven't confirmed a scale/offset for.
    Resume one to keep refining it, or delete it if it turned out to be a
    dead end. Nothing here reaches the firmware's compiled catalog until you
    resume an entry and set its Status to Active."""

    COLUMNS = ["DID", "Name", "Unit", "Notes", "Samples", "Live?", "Updated"]

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
        self.table.horizontalHeader().setSectionResizeMode(3, QHeaderView.Stretch)
        self.table.doubleClicked.connect(lambda *_: self._resume_selected())
        layout.addWidget(self.table, stretch=1)

        btns = QHBoxLayout()
        btns.addStretch(1)
        del_btn = QPushButton("Delete")
        del_btn.clicked.connect(self._delete_selected)
        btns.addWidget(del_btn)
        resume_btn = QPushButton("Resume…")
        mark_primary(resume_btn)
        resume_btn.clicked.connect(self._resume_selected)
        btns.addWidget(resume_btn)
        layout.addLayout(btns)

        self.refresh(unknown_dids=set())

    def refresh(self, unknown_dids: set[tuple[int, int]]) -> None:
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
            live = did is not None and did in unknown_dids
            vals = [
                f"{did[0]:02X} {did[1]:02X}" if did else "—",
                sig.name or "(unnamed)",
                sig.unit,
                sig.notes.replace("\n", " ") or "—",
                str(len(sig.samples)),
                "● live" if live else "—",
                _fmt_age(time.time(), sig.updated_at),
            ]
            for col, text in enumerate(vals):
                item = QTableWidgetItem(text)
                if col in (0, 4, 6):
                    _mono(item)
                if col == 5 and live:
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
            did = signal.requests[0].did if signal.requests else None
            live_entry = self.window.engine.snapshot().unknown.get(did) if did else None
            samples = list(live_entry.samples) if live_entry else []
            dlg = SignalEditorDialog(self.catalog, existing=signal, live_samples=samples, parent=self)
        if dlg.exec() == QDialog.Accepted:
            self.window.on_signal_saved(dlg.saved_signal, dlg.saved_path, dlg.regenerated)
            if dlg.saved_signal.status == "active":
                self.window._select_page(0)  # graduated -- show it live

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
    you go through the whole registry at once: filter, Toggle Status for a
    fast flip, Edit… to change anything else, or Move Up/Down to reorder
    (see Catalog.move_signal -- only enabled with the "All" filter, since a
    filtered view isn't the registry's real, contiguous order)."""

    COLUMNS = ["ID", "Name", "Status", "Unit", "DID(s)", "Cycle", "Notes"]

    def __init__(self, catalog: Catalog, window) -> None:
        super().__init__()
        self.catalog = catalog
        self.window = window
        self._rows: list[Signal] = []

        layout = QVBoxLayout(self)

        controls = QHBoxLayout()
        controls.addWidget(QLabel("Filter:"))
        self.filter = QComboBox()
        self.filter.addItems(["All", "Active only", "WIP only"])
        self.filter.currentIndexChanged.connect(self.refresh)
        controls.addWidget(self.filter)
        controls.addStretch(1)
        self.count_label = QLabel()
        controls.addWidget(self.count_label)
        layout.addLayout(controls)

        self.table = QTableWidget(0, len(self.COLUMNS))
        self.table.setHorizontalHeaderLabels(self.COLUMNS)
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.table.horizontalHeader().setSectionResizeMode(6, QHeaderView.Stretch)
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
        toggle_btn = QPushButton("Toggle Status")
        toggle_btn.setToolTip(
            "Flip the selected signal(s) between Active and WIP without "
            "opening the editor. Run Regenerate afterward to sync the "
            "firmware's compiled catalog."
        )
        toggle_btn.clicked.connect(self._toggle_selected)
        btns.addWidget(toggle_btn)
        edit_btn = QPushButton("Edit…")
        edit_btn.clicked.connect(self._edit_selected)
        btns.addWidget(edit_btn)
        regen_btn = QPushButton("Regenerate Firmware Catalog")
        mark_primary(regen_btn)
        regen_btn.setToolTip(
            "Run tools/gen_catalog.py so signal_catalog.generated.inc "
            "reflects any status changes made here."
        )
        regen_btn.clicked.connect(self._regenerate)
        btns.addWidget(regen_btn)
        layout.addLayout(btns)

        self.refresh()

    def refresh(self) -> None:
        f = self.filter.currentText()
        signals = self.catalog.signals
        if f == "Active only":
            signals = [s for s in signals if s.status == "active"]
        elif f == "WIP only":
            signals = [s for s in signals if s.status == "wip"]
        self._rows = signals

        self.table.setRowCount(len(signals))
        for row, sig in enumerate(signals):
            dids = "; ".join(f"{r.byte1:02X} {r.byte2:02X}" for r in sig.requests) or "—"
            vals = [
                str(sig.id),
                sig.name or "(unnamed)",
                "Active" if sig.status == "active" else "WIP",
                sig.unit,
                dids,
                str(sig.cycle_time_ms),
                sig.notes.replace("\n", " ") or "—",
            ]
            for col, text in enumerate(vals):
                item = QTableWidgetItem(text)
                if col in (0, 5):
                    item.setTextAlignment(Qt.AlignRight | Qt.AlignVCenter)
                if col in (0, 4, 5):
                    _mono(item)
                if col == 2:
                    item.setForeground(QColor(GREEN if sig.status == "active" else AMBER))
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
        dlg = _open_signal_editor(self.catalog, selected[0], parent=self)
        if dlg.exec() == QDialog.Accepted:
            self.window.on_signal_saved(dlg.saved_signal, dlg.saved_path, dlg.regenerated)

    def _new_signal(self) -> None:
        dlg = SignalEditorDialog(self.catalog, parent=self)
        if dlg.exec() == QDialog.Accepted:
            self.window.on_signal_saved(dlg.saved_signal, dlg.saved_path, dlg.regenerated)

    def _new_compound_signal(self) -> None:
        dlg = CompoundSignalDialog(self.catalog, parent=self)
        if dlg.exec() == QDialog.Accepted:
            self.window.on_signal_saved(dlg.saved_signal, dlg.saved_path, dlg.regenerated)

    def _toggle_selected(self) -> None:
        selected = self._selected_signals()
        if not selected:
            QMessageBox.information(self, "Registry", "Select at least one signal first.")
            return
        for sig in selected:
            sig.status = "wip" if sig.status == "active" else "active"
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
        if any(s.status == "active" for s in selected):
            msg += (
                "\n\nThis includes ACTIVE signal(s) currently compiled into "
                "the firmware."
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
            subprocess.run(
                ["python3", str(REPO_ROOT / "tools/gen_catalog.py")],
                check=True, capture_output=True, text=True,
            )
        except (subprocess.CalledProcessError, OSError) as e:
            QMessageBox.warning(self, "Regenerate", f"gen_catalog.py failed:\n{e}")
            return
        QMessageBox.information(self, "Regenerate", "Firmware catalog regenerated.")


class MainWindow(QMainWindow):
    PAGES = ["Signals", "Traffic", "Registry", "Learn", "WIP"]

    def __init__(self, port: str | None, demo: bool, listen_only: bool) -> None:
        super().__init__()
        self.setWindowTitle("MQB Monitor — UDS CAN decoder")
        self.resize(1080, 680)

        self.catalog = Catalog.load()
        self.engine = MonitorEngine(self.catalog)
        self.settings = QSettings("MQBMonitor", "monitor")
        self.source = None
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
        for page in (
            self.signals_tab, self.traffic_tab, self.registry_tab,
            self.learn_tab, self.wip_tab,
        ):
            self.stack.addWidget(page)
        body_layout.addWidget(self.stack, stretch=1)

        root_layout.addWidget(body, stretch=1)
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

        self.status_dot = QLabel("●")
        self.status_dot.setObjectName("StatusDot")
        self.status_dot.setStyleSheet(f"color: {TEXT_DIM};")
        layout.addWidget(self.status_dot)
        self.status = QLabel("Disconnected")
        self.status.setObjectName("StatsLabel")
        self.status.setFont(MONO_FONT)
        # Fixed + right-aligned so growing frame/rate/DID counts don't keep
        # reflowing the whole trailing group of the bar every tick.
        self.status.setMinimumWidth(340)
        self.status.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(self.status)

        return bar

    def _populate_ports(self) -> None:
        current = self.source_combo.currentData() if self.source_combo.count() else None
        self.source_combo.clear()
        self.source_combo.addItem("Demo (synthesized traffic)", userData="__demo__")
        for dev, desc in available_ports():
            self.source_combo.addItem(f"{dev} — {desc}", userData=dev)
        if self._preselect_demo:
            self.source_combo.setCurrentIndex(0)
        else:
            target = self._preselect_port or current or guess_teensy_port()
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
        choice = self.source_combo.currentData()
        try:
            if choice == "__demo__":
                self.source = DemoSource(self.catalog, self.engine.feed)
            else:
                self.source = SlcanSerialSource(
                    choice, self.engine.feed,
                    listen_only=self.listen_check.isChecked(),
                    can_baud=self.baud_combo.currentData(),
                )
            self.source.start()
        except Exception as e:  # noqa: BLE001 -- surface any connect failure
            self.source = None
            QMessageBox.critical(self, "Connect failed", str(e))
            return
        self.connect_btn.setText("Disconnect")
        self.source_combo.setEnabled(False)
        self.listen_check.setEnabled(False)
        self.baud_combo.setEnabled(False)

    def _disconnect(self) -> None:
        if self.source is not None:
            self.source.stop()
            self.source = None
        self.connect_btn.setText("Connect")
        self.source_combo.setEnabled(True)
        self.listen_check.setEnabled(True)
        self.baud_combo.setEnabled(True)

    def on_signal_saved(self, signal, path, regenerated: bool) -> None:
        """Called whenever SignalEditorDialog commits a save (from Learn,
        WIP-resume, or the Registry tab). Refreshes every view that depends
        on the registry. Deliberately doesn't navigate or pop up a message --
        callers decide navigation themselves (Learn/WIP want to jump to
        where the signal landed; Registry wants to stay put for batch
        review), and the tables updating live is confirmation enough."""
        self.engine.refresh_catalog()
        self.signals_tab.refresh_catalog()
        if signal is not None and signal.status == "active":
            self.signals_tab.add_watch(signal.id)
        self.registry_tab.refresh()
        self.wip_tab.refresh(set(self.engine.snapshot().unknown.keys()))

    def on_wip_saved(self) -> None:
        """WIP tab's own Delete action, which mutates the catalog directly
        without going through SignalEditorDialog."""
        self.engine.refresh_catalog()
        self.registry_tab.refresh()
        self.wip_tab.refresh(set(self.engine.snapshot().unknown.keys()))

    def on_registry_changed(self) -> None:
        """Registry tab's Toggle Status / Delete, which also mutate the
        catalog directly."""
        self.engine.refresh_catalog()
        self.signals_tab.refresh_catalog()
        self.learn_tab.update_view(self.engine.snapshot())
        self.wip_tab.refresh(set(self.engine.snapshot().unknown.keys()))

    def _tick(self) -> None:
        snap = self.engine.snapshot()
        is_metric = self.metric_check.isChecked()
        self.signals_tab.update_view(snap, is_metric)
        self.traffic_tab.update_view(snap, snap.started_at)
        self.learn_tab.update_view(snap)
        self.wip_tab.refresh(set(snap.unknown.keys()))

        rate = snap.total_frames / max(1e-3, time.monotonic() - snap.started_at)
        if isinstance(self.source, DemoSource):
            conn, dot_color = "Demo", ACCENT
        elif self.source:
            conn, dot_color = "Connected", GREEN
        else:
            conn, dot_color = "Disconnected", TEXT_DIM
        self.status_dot.setStyleSheet(f"color: {dot_color};")
        self.status.setText(
            f"{conn} · {snap.total_frames} frames · {rate:.0f}/s · "
            f"{len(snap.unknown)} unknown DIDs"
        )

    def closeEvent(self, event) -> None:  # noqa: N802 -- Qt override
        self._disconnect()
        super().closeEvent(event)


def run_gui(port: str | None, demo: bool, listen_only: bool) -> int:
    app = QApplication.instance() or QApplication([])
    apply_theme(app)
    win = MainWindow(port=port, demo=demo, listen_only=listen_only)
    win.show()
    return app.exec()
