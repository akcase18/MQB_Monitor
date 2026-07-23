"""PySide6 GUI for the MQB monitor.

A sidebar shell over a shared MonitorEngine, polled on a timer, with four
pages:
  * Signals -- a user-built watchlist of known catalog signals, decoded live.
  * Traffic -- rolling decoded UDS message log (request/response, latency),
               with CSV export (decoded or raw frames) via Save Log.
  * Learn   -- unknown DIDs seen on the bus; commit one straight to the
               catalog, or save it as WIP if you don't have a confirmed
               scale/offset yet.
  * WIP     -- signals saved as WIP from Learn: resume to keep refining, or
               delete. Nothing here reaches signals.json until promoted.
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
    DemoSource,
    SlcanSerialSource,
    available_ports,
    guess_teensy_port,
)
from .theme import (
    ACCENT,
    GREEN,
    MONO_FONT,
    TEXT_DIM,
    apply_theme,
    mark_primary,
)
from .wip import MAX_SAMPLES, WipSignal, WipStore

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

    def __init__(
        self, catalog: Catalog, engine: MonitorEngine, wip_store: WipStore, window
    ) -> None:
        super().__init__()
        self.catalog = catalog
        self.engine = engine
        self.wip_store = wip_store
        self.window = window
        self._dids: list[tuple[int, int]] = []

        layout = QVBoxLayout(self)
        info = QLabel(
            "Unknown DIDs seen on the bus (an OBDEleven query with no catalog "
            "match). Select one and click Add to define it — you can commit "
            "it straight to the catalog, or save it as WIP if you don't have "
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
            is_wip = self.wip_store.get(u.did) is not None
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
        wip = self.wip_store.get(did)
        dlg = LearnDialog(did, samples, self.catalog, self.wip_store, wip=wip, parent=self)
        if dlg.exec() == QDialog.Accepted:
            if dlg.saved_kind == "catalog":
                self.window.on_signal_learned(
                    dlg.saved_path, dlg.regenerated, dlg.saved_signal_id
                )
            elif dlg.saved_kind == "wip":
                self.window.on_wip_saved()


class LearnDialog(QDialog):
    """Turns an unknown DID into either a saved WIP note (offset/scale not
    yet confirmed -- see wip.py) or a committed catalog signal. Can be opened
    fresh (wip=None) or resuming previously saved WIP notes for this DID,
    which pre-fill every field."""

    def __init__(
        self,
        did,
        samples,
        catalog: Catalog,
        wip_store: WipStore,
        wip: WipSignal | None = None,
        parent=None,
    ) -> None:
        super().__init__(parent)
        self.did = did
        self.catalog = catalog
        self.wip_store = wip_store
        self.saved_path: Path | None = None
        self.saved_signal_id: int | None = None
        self.saved_kind: str | None = None  # "catalog" | "wip" | None (cancelled)
        self.regenerated = False

        # Merge this session's live samples with any samples saved earlier
        # alongside the WIP note, newest-last, capped like the engine's own
        # unknown-DID buffer.
        merged = [bytes.fromhex(h) for h in (wip.samples if wip else [])] + list(samples)
        self._samples = merged[-MAX_SAMPLES:]
        last = self._samples[-1] if self._samples else b""
        self._last_raw = decode_raw_payload(last) if last else 0.0

        self.setWindowTitle(
            f"{'Resume' if wip else 'Learn'} DID {did[0]:02X} {did[1]:02X}"
        )
        self.setMinimumWidth(480)

        form = QFormLayout(self)
        header = (
            f"<b>DID {did[0]:02X} {did[1]:02X}</b> — "
            f"{len(self._samples)} sample(s), last payload "
            f"[{last.hex(' ') or 'none'}], decoded raw = {self._last_raw:.0f}"
        )
        if wip:
            header += "<br><i>Resuming saved WIP notes.</i>"
        form.addRow(QLabel(header))

        self.name = QLineEdit(wip.name if wip else "")
        self.name.setPlaceholderText("e.g. Oil Pressure (optional for WIP)")
        form.addRow("Name", self.name)

        self.unit = QComboBox()
        self.unit.addItems(UNIT_TYPES)
        if wip and wip.unit in UNIT_TYPES:
            self.unit.setCurrentText(wip.unit)
        form.addRow("Unit", self.unit)

        self.cycle = QSpinBox()
        self.cycle.setRange(10, 60000)
        self.cycle.setValue(wip.cycle_time_ms if wip else 100)
        self.cycle.setSuffix(" ms")
        form.addRow("Cycle time", self.cycle)

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

        self.offset = self._dspin(wip.offset if wip and wip.offset is not None else 0.0)
        self.scale = self._dspin(wip.scale if wip and wip.scale is not None else 1.0)
        self.scale.setDecimals(6)
        self.offset.setDecimals(4)
        self.scale.valueChanged.connect(self._update_preview)
        self.offset.valueChanged.connect(self._update_preview)
        form.addRow("Offset", self.offset)
        form.addRow("Scale", self.scale)
        unconfirmed_hint = QLabel(
            "Not confirmed yet? Leave the defaults and use "
            "<b>Save as WIP</b> below instead of committing to the catalog."
        )
        unconfirmed_hint.setWordWrap(True)
        unconfirmed_hint.setStyleSheet(f"color: {TEXT_DIM}; font-size: 11px;")
        form.addRow("", unconfirmed_hint)

        self.dmin = self._dspin(wip.display_min if wip else 0.0)
        self.dmax = self._dspin(wip.display_max if wip else 100.0)
        form.addRow("Display min", self.dmin)
        form.addRow("Display max", self.dmax)

        self.preview = QLabel()
        form.addRow("Preview", self.preview)
        self._update_preview()

        self.notes = QPlainTextEdit(wip.notes if wip else "")
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
        wip_btn = QPushButton("Save as WIP")
        wip_btn.setToolTip(
            "Save name/unit/notes/samples (and offset/scale if you've set "
            "them) without touching the real catalog or the firmware."
        )
        wip_btn.clicked.connect(self._save_wip)
        buttons.addWidget(wip_btn)
        catalog_btn = QPushButton("Add to Catalog")
        mark_primary(catalog_btn)
        catalog_btn.clicked.connect(self._save)
        buttons.addWidget(catalog_btn)
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

    def _save(self) -> None:
        name = self.name.text().strip()
        if not name:
            QMessageBox.warning(self, "Save", "Give the signal a name.")
            return
        signal = Signal(
            id=self.catalog.next_id(),
            name=name,
            unit=self.unit.currentText(),
            cycle_time_ms=self.cycle.value(),
            transform="default",
            display_min=self.dmin.value(),
            display_max=self.dmax.value(),
            requests=[Request(self.did[0], self.did[1], self.offset.value(), self.scale.value())],
        )
        self.catalog.add_signal(signal)
        self.saved_signal_id = signal.id
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

        # This DID just graduated -- drop its now-redundant WIP note, if any.
        if self.wip_store.get(self.did) is not None:
            self.wip_store.remove(self.did)
            self.wip_store.save()

        self.saved_kind = "catalog"
        self.accept()

    def _save_wip(self) -> None:
        wip_signal = WipSignal(
            did=self.did,
            name=self.name.text().strip(),
            unit=self.unit.currentText(),
            cycle_time_ms=self.cycle.value(),
            notes=self.notes.toPlainText().strip(),
            offset=self.offset.value(),
            scale=self.scale.value(),
            display_min=self.dmin.value(),
            display_max=self.dmax.value(),
            samples=[s.hex() for s in self._samples],
        )
        self.wip_store.upsert(wip_signal)
        self.wip_store.save()
        self.saved_kind = "wip"
        self.accept()


class WipTab(QWidget):
    """Every saved-but-not-yet-committed signal: a holding area for DIDs
    you've started reverse-engineering but haven't confirmed a scale/offset
    for. Resume one to keep refining it, or delete it if it turned out to be
    a dead end. Nothing here ever reaches signals.json until you resume an
    entry and explicitly hit Add to Catalog."""

    COLUMNS = ["DID", "Name", "Unit", "Notes", "Samples", "Live?", "Updated"]

    def __init__(self, wip_store: WipStore, catalog: Catalog, window) -> None:
        super().__init__()
        self.wip_store = wip_store
        self.catalog = catalog
        self.window = window
        self._dids: list[tuple[int, int]] = []

        layout = QVBoxLayout(self)
        info = QLabel(
            "Signals you've started figuring out but haven't confirmed a "
            "scale/offset for yet. Nothing here affects the dashboard or "
            "firmware until you resume one and commit it to the catalog."
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
        entries = self.wip_store.all()
        self._dids = [w.did for w in entries]

        empty = not entries
        self.hint.setVisible(empty)
        self.table.setVisible(not empty)
        if empty:
            self.table.setRowCount(0)
            return

        self.table.setRowCount(len(entries))
        for row, w in enumerate(entries):
            live = w.did in unknown_dids
            vals = [
                w.did_str,
                w.name or "(unnamed)",
                w.unit,
                w.notes.replace("\n", " ") or "—",
                str(len(w.samples)),
                "● live" if live else "—",
                _fmt_age(time.time(), w.updated_at),
            ]
            for col, text in enumerate(vals):
                item = QTableWidgetItem(text)
                if col in (0, 4, 6):
                    _mono(item)
                if col == 5 and live:
                    item.setForeground(QColor(GREEN))
                self.table.setItem(row, col, item)

    def _selected_did(self) -> tuple[int, int] | None:
        row = self.table.currentRow()
        if row < 0 or row >= len(self._dids):
            return None
        return self._dids[row]

    def _resume_selected(self) -> None:
        did = self._selected_did()
        if did is None:
            QMessageBox.information(self, "WIP", "Select a WIP signal first.")
            return
        wip = self.wip_store.get(did)
        if wip is None:
            return
        live_samples = self.window.engine.snapshot().unknown.get(did)
        samples = list(live_samples.samples) if live_samples else []
        dlg = LearnDialog(did, samples, self.catalog, self.wip_store, wip=wip, parent=self)
        if dlg.exec() == QDialog.Accepted:
            if dlg.saved_kind == "catalog":
                self.window.on_signal_learned(
                    dlg.saved_path, dlg.regenerated, dlg.saved_signal_id
                )
            elif dlg.saved_kind == "wip":
                self.window.on_wip_saved()

    def _delete_selected(self) -> None:
        did = self._selected_did()
        if did is None:
            QMessageBox.information(self, "WIP", "Select a WIP signal first.")
            return
        wip = self.wip_store.get(did)
        name = (wip.name if wip else "") or f"DID {did[0]:02X} {did[1]:02X}"
        if QMessageBox.question(
            self, "Delete WIP signal", f"Delete WIP notes for “{name}”?"
        ) != QMessageBox.Yes:
            return
        self.wip_store.remove(did)
        self.wip_store.save()
        self.window.on_wip_saved()


class MainWindow(QMainWindow):
    PAGES = ["Signals", "Traffic", "Learn", "WIP"]

    def __init__(self, port: str | None, demo: bool, listen_only: bool) -> None:
        super().__init__()
        self.setWindowTitle("MQB Monitor — UDS CAN decoder")
        self.resize(1080, 680)

        self.catalog = Catalog.load()
        self.engine = MonitorEngine(self.catalog)
        self.wip_store = WipStore()
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
        self.learn_tab = LearnTab(self.catalog, self.engine, self.wip_store, self)
        self.wip_tab = WipTab(self.wip_store, self.catalog, self)
        for page in (self.signals_tab, self.traffic_tab, self.learn_tab, self.wip_tab):
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

        self.connect_btn = QPushButton("Connect")
        mark_primary(self.connect_btn)
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
                )
            self.source.start()
        except Exception as e:  # noqa: BLE001 -- surface any connect failure
            self.source = None
            QMessageBox.critical(self, "Connect failed", str(e))
            return
        self.connect_btn.setText("Disconnect")
        self.source_combo.setEnabled(False)
        self.listen_check.setEnabled(False)

    def _disconnect(self) -> None:
        if self.source is not None:
            self.source.stop()
            self.source = None
        self.connect_btn.setText("Connect")
        self.source_combo.setEnabled(True)
        self.listen_check.setEnabled(True)

    def on_signal_learned(self, path, regenerated: bool, signal_id) -> None:
        self.engine.refresh_catalog()
        self.signals_tab.refresh_catalog()
        if signal_id is not None:
            self.signals_tab.add_watch(signal_id)  # watch what you just learned
        self.wip_tab.refresh(set(self.engine.snapshot().unknown.keys()))
        self._select_page(0)
        msg = f"Saved to {path}."
        if regenerated:
            msg += "\nFirmware catalog regenerated (rebuild/flash to use it)."
        else:
            msg += "\nRun tools/gen_catalog.py before rebuilding the firmware."
        QMessageBox.information(self, "Signal added", msg)

    def on_wip_saved(self) -> None:
        self.wip_tab.refresh(set(self.engine.snapshot().unknown.keys()))
        self._select_page(3)

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
