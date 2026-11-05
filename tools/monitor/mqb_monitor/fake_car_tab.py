"""The Fake Car tab: pick registry signals to fake, set exactly what each
one sends (a value in its units, the raw field value, an enum label, or the
payload bytes themselves), step or sweep it, and save what the phone shows
as a point on the signal.

The emulator itself is emulator.FakeCar, owned by the main window, which
feeds it the bus and decides whether it may answer (see
MainWindow.start_fake_car).
"""
from __future__ import annotations

import math
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QSlider,
    QSpinBox,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from .capture import CAPTURE_DIR, read_capture
from .catalog import (
    Catalog,
    Point,
    Signal,
    concrete_field,
    decode_field,
    encode_field,
    field_range,
    unit_decimals,
    unit_label,
)
from .emulator import FakeCar, Recording, Sweep
from .solver import Candidate, display_tolerance, search_enum_fields, search_fields
from .theme import ACCENT, AMBER, GREEN, MONO_FONT, RED, TEXT_DIM, mark_primary

# QSettings key: the recording to load again on the next launch.
RECORDING_SETTING = "fake_car/recording"

# QSpinBox is 32-bit signed.
_SPIN_MIN, _SPIN_MAX = -(2**31), 2**31 - 1
FREEZE_STEPS = 1000



@dataclass
class _Faked:
    signal: Signal
    tx: int
    rx: int
    guessed: bool = False  # payload length made up: no recording or sample

    @property
    def did(self) -> tuple[int, int]:
        return self.signal.requests[0].did

    @property
    def req(self):
        return self.signal.requests[0]


def _cell(text: str, mono: bool = False) -> QTableWidgetItem:
    item = QTableWidgetItem(text)
    if mono:
        item.setFont(MONO_FONT)
    return item


class FakeCarTab(QWidget):
    def __init__(self, catalog: Catalog, window) -> None:
        super().__init__()
        self.catalog = catalog
        self.window = window
        self.fake: FakeCar = window.fake
        self._faked: list[_Faked] = []
        # (module tx, DID) whose payload was set here: those keep it. Every
        # other signal answers from the recording, or from a stand-in when
        # the recording never asked for it.
        self._edited: set[tuple[int, tuple[int, int]]] = set()
        self._current: _Faked | None = None
        self._sweep: Sweep | None = None
        self._auto_next: float | None = None
        self._asked_total = -1

        layout = QVBoxLayout(self)

        info = QLabel(
            "Answer the OBDEleven dongle as if this were the car, on a bench "
            "OBD socket wired to the dash or sniffer — never in the car. Every "
            "registry signal is here: pick one, set what it sends, and save "
            "what the phone shows as a point. Load a recording of the dongle "
            "talking to the car so it accepts the fake one."
        )
        info.setWordWrap(True)
        layout.addWidget(info)

        top = QHBoxLayout()
        self.run_btn = QPushButton("Start Fake Car")
        self.run_btn.setCheckable(True)
        mark_primary(self.run_btn)
        self.run_btn.toggled.connect(self._toggle_run)
        top.addWidget(self.run_btn)
        self.state_label = QLabel()
        top.addWidget(self.state_label)
        top.addStretch(1)
        layout.addLayout(top)

        rec_row = QHBoxLayout()
        rec_row.addWidget(QLabel("Recording:"))
        self.rec_label = QLabel("none")
        self.rec_label.setStyleSheet(f"color: {TEXT_DIM};")
        # Wraps: a recording's summary (modules, repeats) runs long enough
        # to widen the whole window otherwise.
        self.rec_label.setWordWrap(True)
        rec_row.addWidget(self.rec_label, stretch=1)
        load = QPushButton("Load Recording…")
        load.clicked.connect(self._load_recording)
        rec_row.addWidget(load)
        clear_rec = QPushButton("Clear")
        clear_rec.clicked.connect(self._clear_recording)
        rec_row.addWidget(clear_rec)
        rec_row.addWidget(QLabel("Answer as at"))
        self.freeze = QSlider(Qt.Horizontal)
        self.freeze.setRange(0, FREEZE_STEPS)
        self.freeze.setValue(FREEZE_STEPS)
        self.freeze.setFixedWidth(180)
        self.freeze.setEnabled(False)
        self.freeze.setToolTip("Which moment of the recording to answer with, "
                               "for DIDs that aren't being faked.")
        self.freeze.valueChanged.connect(self._on_freeze)
        rec_row.addWidget(self.freeze)
        self.freeze_label = QLabel("latest")
        self.freeze_label.setFont(MONO_FONT)
        self.freeze_label.setMinimumWidth(70)
        rec_row.addWidget(self.freeze_label)
        layout.addLayout(rec_row)

        split = QSplitter(Qt.Horizontal)
        layout.addWidget(split, stretch=3)

        # Left: the faked signals.
        left = QWidget()
        lv = QVBoxLayout(left)
        lv.setContentsMargins(0, 0, 0, 0)
        self.table = QTableWidget(0, 5)
        self.table.setHorizontalHeaderLabels(["Signal", "Module", "DID", "Payload", "Should show"])
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.setSelectionMode(QTableWidget.SingleSelection)
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        self.table.currentCellChanged.connect(lambda row, *_: self._select(row))
        self.filter = QLineEdit()
        self.filter.setPlaceholderText("Filter by name, id, module or DID")
        self.filter.textChanged.connect(self._apply_filter)
        lv.addWidget(self.filter)
        lv.addWidget(self.table)
        btns = QHBoxLayout()
        reset = QPushButton("Reset")
        reset.setToolTip("Stop faking the selected signal: answer it from the "
                         "recording again.")
        reset.clicked.connect(self._reset_signal)
        btns.addWidget(reset)
        btns.addStretch(1)
        lv.addLayout(btns)
        split.addWidget(left)

        # Right: what the selected one sends.
        self.controls = QWidget()
        outer = QVBoxLayout(self.controls)
        outer.setContentsMargins(8, 0, 0, 0)
        form = QFormLayout()
        outer.addLayout(form)
        outer.addStretch(1)  # spare height below the form, not between rows
        self._form = form
        self.title = QLabel()
        form.addRow(self.title)

        self.payload_edit = QLineEdit()
        self.payload_edit.setFont(MONO_FONT)
        self.payload_edit.setToolTip("The whole payload after the DID, in hex. "
                                     "Edit any byte; Enter to send it.")
        self.payload_edit.editingFinished.connect(self._on_payload_edited)
        self.guess_label = QLabel(
            "Length guessed — no recording or sample of this DID. The phone "
            "may reject an answer of the wrong length; fix it here.")
        self.guess_label.setWordWrap(True)
        self.guess_label.setStyleSheet(f"color: {AMBER};")
        payload_box = QWidget()
        pv = QVBoxLayout(payload_box)
        pv.setContentsMargins(0, 0, 0, 0)
        pv.addWidget(self.payload_edit)
        pv.addWidget(self.guess_label)
        form.addRow("Payload", payload_box)
        self.field_label = QLabel()
        self.field_label.setStyleSheet(f"color: {TEXT_DIM};")
        form.addRow("Field", self.field_label)

        self.value_spin = QDoubleSpinBox()
        self.value_spin.setRange(-1e9, 1e9)
        self.value_spin.setKeyboardTracking(False)
        self.value_spin.valueChanged.connect(self._on_value)
        form.addRow("Value", self.value_spin)
        self.enum_combo = QComboBox()
        self.enum_combo.activated.connect(self._on_enum)
        form.addRow("State", self.enum_combo)
        self.raw_spin = QSpinBox()
        self.raw_spin.setKeyboardTracking(False)
        self.raw_spin.valueChanged.connect(self._on_raw)
        form.addRow("Raw", self.raw_spin)
        self.shows = QLabel()
        self.shows.setFont(MONO_FONT)
        form.addRow("Should show", self.shows)

        sweep_row = QHBoxLayout()
        self.sweep_from = QSpinBox()
        self.sweep_to = QSpinBox()
        self.sweep_step = QSpinBox()
        self.sweep_step.setRange(1, _SPIN_MAX)
        for label, sp in (("from", self.sweep_from), ("to", self.sweep_to),
                          ("step", self.sweep_step)):
            sweep_row.addWidget(QLabel(label))
            sweep_row.addWidget(sp)
            sp.valueChanged.connect(self._reset_sweep)
        form.addRow("Sweep raw", sweep_row)
        step_row = QHBoxLayout()
        back = QPushButton("◀ Step")
        back.clicked.connect(lambda: self._step(-1))
        fwd = QPushButton("Step ▶")
        fwd.clicked.connect(lambda: self._step(1))
        step_row.addWidget(back)
        step_row.addWidget(fwd)
        self.auto = QCheckBox("Auto every")
        self.auto.toggled.connect(self._on_auto)
        step_row.addWidget(self.auto)
        self.interval = QDoubleSpinBox()
        self.interval.setRange(0.2, 60.0)
        self.interval.setValue(3.0)
        self.interval.setSuffix(" s")
        step_row.addWidget(self.interval)
        step_row.addStretch(1)
        form.addRow("", step_row)

        point_row = QHBoxLayout()
        self.phone_value = QLineEdit()
        self.phone_value.setPlaceholderText("what the phone shows")
        self.phone_value.returnPressed.connect(self._add_point)
        point_row.addWidget(self.phone_value)
        add_point = QPushButton("Add Point")
        add_point.setToolTip("Save the payload being sent now, with this "
                             "value, as a point on the signal. Enter does "
                             "the same.")
        add_point.clicked.connect(self._add_point)
        point_row.addWidget(add_point)
        form.addRow("Phone shows", point_row)

        self.points = QTableWidget(0, 4)
        self.points.setHorizontalHeaderLabels(["Payload", "Raw", "Phone", "Best fit"])
        self.points.verticalHeader().setVisible(False)
        self.points.setEditTriggers(QTableWidget.NoEditTriggers)
        self.points.setSelectionBehavior(QTableWidget.SelectRows)
        self.points.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        self.points.setMinimumHeight(110)
        self.points.setMaximumHeight(170)
        form.addRow("Points", self.points)
        fit_row = QHBoxLayout()
        self.fit_label = QLabel()
        self.fit_label.setWordWrap(True)
        fit_row.addWidget(self.fit_label, stretch=1)
        rm_point = QPushButton("Remove Point")
        rm_point.clicked.connect(self._remove_point)
        fit_row.addWidget(rm_point)
        self.use_fit = QPushButton("Use This Fit")
        self.use_fit.setToolTip("Save this field and scale/offset (or state "
                                "labels) on the signal.")
        self.use_fit.clicked.connect(self._use_fit)
        fit_row.addWidget(self.use_fit)
        form.addRow("", fit_row)
        self._best: Candidate | None = None
        split.addWidget(self.controls)
        split.setSizes([480, 560])

        # Bottom: what the dongle asked for.
        asked_head = QHBoxLayout()
        asked_head.addWidget(QLabel("What the dongle asked"))
        asked_head.addStretch(1)
        clear_log = QPushButton("Clear")
        clear_log.clicked.connect(self._clear_log)
        asked_head.addWidget(clear_log)
        layout.addLayout(asked_head)
        self.asked = QTableWidget(0, 4)
        self.asked.setHorizontalHeaderLabels(["To", "Request", "Times", "Answered with"])
        self.asked.verticalHeader().setVisible(False)
        self.asked.setEditTriggers(QTableWidget.NoEditTriggers)
        self.asked.horizontalHeader().setSectionResizeMode(1, QHeaderView.Stretch)
        layout.addWidget(self.asked, stretch=2)

        self.refresh_signals()
        last = self.window.settings.value(RECORDING_SETTING)
        if last and Path(last).is_file():
            self.load_recording_file(last, quiet=True)
        self._show_state()

    # --- running ---

    def _toggle_run(self, on: bool) -> None:
        if on and not self.window.start_fake_car():
            self.run_btn.setChecked(False)
            return
        if not on:
            self.window.stop_fake_car()
        self.run_btn.setText("Stop Fake Car" if on else "Start Fake Car")
        self._show_state()

    def stopped(self) -> None:
        """The window stopped it (e.g. on Disconnect)."""
        self.run_btn.setChecked(False)

    def _show_state(self) -> None:
        if self.run_btn.isChecked():
            n = len(self.fake.modules())
            self.state_label.setText(
                f"Answering as {n} module{'s' if n != 1 else ''} · "
                f"{self.fake.answered} answers sent")
            self.state_label.setStyleSheet(f"color: {GREEN};")
        else:
            self.state_label.setText("Off — the dongle's requests go unanswered")
            self.state_label.setStyleSheet(f"color: {TEXT_DIM};")

    # --- recording ---

    def _load_recording(self) -> None:
        start = CAPTURE_DIR if CAPTURE_DIR.is_dir() else Path.home()
        path, _ = QFileDialog.getOpenFileName(
            self, "Load Recording", str(start), "Captures (*.csv)")
        if path:
            self.load_recording_file(path)

    def load_recording_file(self, path: str, quiet: bool = False) -> None:
        """Loads a recording and remembers it for the next launch. `quiet`
        (that next launch) skips the error popups."""
        try:
            info, frames = read_capture(Path(path))
        except (OSError, ValueError) as e:
            if not quiet:
                QMessageBox.warning(self, "Load Recording", str(e))
            return
        rec = Recording.from_frames(frames)
        if not rec.exchanges:
            if not quiet:
                QMessageBox.warning(self, "Load Recording",
                                    "No diagnostic requests in that capture.")
            return
        self.window.settings.setValue(RECORDING_SETTING, str(path))
        self.fake.recording = rec
        mods = ", ".join(f"{tx:03X}/{rx:03X}" for tx, rx in sorted(rec.modules.items()))
        repeats = ", ".join(
            f"{b.can_id:0{8 if b.extended else 3}X} every {b.period:.1f} s"
            for b in rec.background)
        self.rec_label.setText(
            f"{Path(path).name} — {len(rec.exchanges)} requests, "
            f"{rec.duration:.0f} s, modules {mods or 'none'}"
            + (f"; repeats {repeats}" if repeats else "")
            + ("  (demo traffic)" if info.is_demo else ""))
        self.rec_label.setStyleSheet("")
        self.freeze.setEnabled(True)
        self.freeze.setValue(FREEZE_STEPS)
        self._sync_overrides()

    def _clear_recording(self) -> None:
        self.window.settings.remove(RECORDING_SETTING)
        self.fake.recording = None
        self.fake.freeze_at = None
        self.rec_label.setText("none")
        self.rec_label.setStyleSheet(f"color: {TEXT_DIM};")
        self.freeze.setEnabled(False)
        self._sync_overrides()

    def _on_freeze(self, v: int) -> None:
        rec = self.fake.recording
        if rec is None or v >= FREEZE_STEPS:
            self.fake.freeze_at = None
            self.freeze_label.setText("latest")
            self._sync_overrides()
            return
        self.fake.freeze_at = rec.duration * v / FREEZE_STEPS
        self.freeze_label.setText(f"{self.fake.freeze_at:.1f} s")

    # --- faked signals ---
        self._sync_overrides()

    def _base_payload(self, sig: Signal, tx: int) -> tuple[bytes, bool]:
        """What a newly faked signal starts out sending: the recording's
        answer, else its newest stored sample, else the newest seen on the
        bus this session, else zeros -- with True when the length is that
        guess."""
        req = sig.requests[0]
        rec = self.fake.recording
        if rec is not None:
            got = rec.did_payload(tx, req.did, self.fake.freeze_at)
            if got:
                return got, False
        if sig.samples:
            return bytes.fromhex(sig.samples[-1]), False
        seen = self.window.engine.latest(req.did, req.ecu.rx if req.ecu else None)
        if seen:
            return seen[0], False
        fld = concrete_field(req.field, 2)
        return bytes(max(fld.start + fld.length, 2)), True

    # --- the signal list: the whole registry ---

    def refresh_signals(self) -> None:
        """Lists every single-request registry signal with a module, and
        works out what each answers with. Called again whenever the
        registry changes."""
        keep = self._current.signal.id if self._current else None
        old = {f.signal.id: f for f in self._faked}
        self._faked = []
        for sig in sorted(self.catalog.signals, key=lambda s: s.id):
            if len(sig.requests) != 1 or sig.requests[0].ecu is None:
                continue
            ecu = sig.requests[0].ecu
            was = old.get(sig.id)
            if was is not None and (was.tx, was.rx) == (ecu.tx, ecu.rx):
                was.signal = sig
                self._faked.append(was)
            else:
                self._faked.append(_Faked(sig, ecu.tx, ecu.rx))
        self._sync_overrides()
        row = next((i for i, f in enumerate(self._faked) if f.signal.id == keep), -1)
        self.table.blockSignals(True)
        if row >= 0:
            self.table.setCurrentCell(row, 0)
        else:
            self.table.clearSelection()
        self.table.blockSignals(False)
        self._select(row)

    def _sync_overrides(self) -> None:
        """Payloads set here stay. Otherwise a signal the recording answered
        is answered from it (as of Answer as at), and any other gets a
        stand-in payload so its module still answers."""
        rec = self.fake.recording
        wanted = {}
        for f in self._faked:
            key = (f.tx, f.did)
            f.guessed = False
            if key in self._edited or key in wanted:
                continue
            if rec is not None and rec.did_payload(f.tx, f.did, self.fake.freeze_at):
                wanted[key] = None
                continue
            base, f.guessed = self._base_payload(f.signal, f.tx)
            wanted[key] = (f.rx, base)
        for (tx, did), o in list(self.fake.overrides.items()):
            if (tx, did) not in self._edited and wanted.get((tx, did)) is None:
                self.fake.remove_override(tx, did)
        for (tx, did), got in wanted.items():
            if got is not None and self.fake.payload(tx, did) is None:
                self.fake.set_override(tx, got[0], did, got[1])
        self._edited &= {(f.tx, f.did) for f in self._faked}
        self._refresh_table()
        self._sync_from_payload()

    def _payload(self, f: _Faked) -> bytes:
        """What the fake car answers this signal's DID with right now."""
        got = self.fake.payload(f.tx, f.did)
        if got is None and self.fake.recording is not None:
            got = self.fake.recording.did_payload(f.tx, f.did, self.fake.freeze_at)
        return got or b""

    def _reset_signal(self) -> None:
        f = self._current
        if f is None:
            return
        self._edited.discard((f.tx, f.did))
        self.fake.remove_override(f.tx, f.did)
        self._sync_overrides()

    def _apply_filter(self) -> None:
        words = self.filter.text().lower().split()
        for row, f in enumerate(self._faked):
            text = (f"#{f.signal.id} {f.signal.name} {f.tx:03x}/{f.rx:03x} "
                    f"{f.did[0]:02x} {f.did[1]:02x} {f.did[0]:02x}{f.did[1]:02x}").lower()
            self.table.setRowHidden(row, not all(w in text for w in words))

    def _refresh_table(self) -> None:
        self.table.setRowCount(len(self._faked))
        for row, f in enumerate(self._faked):
            payload = self._payload(f)
            value = f.req.decode(payload)
            vals = [
                f"#{f.signal.id} {f.signal.name or '(unnamed)'}",
                f"{f.tx:03X}/{f.rx:03X}",
                f"{f.did[0]:02X} {f.did[1]:02X}",
                payload.hex(" "),
                "—" if value is None else f.signal.format_value(value),
            ]
            edited = (f.tx, f.did) in self._edited
            for col, text in enumerate(vals):
                item = _cell(text, mono=col in (1, 2, 3, 4))
                if edited:
                    item.setForeground(QColor(ACCENT))  # faked, not the recording
                self.table.setItem(row, col, item)
        self._apply_filter()

    def _select(self, row: int) -> None:
        self._current = self._faked[row] if 0 <= row < len(self._faked) else None
        self.controls.setEnabled(self._current is not None)
        self.auto.setChecked(False)
        f = self._current
        self._refresh_points()
        if f is None:
            self.title.setText("Pick a signal to fake.")
            return
        self.title.setText(f"<b>#{f.signal.id} {f.signal.name or '(unnamed)'}</b> — "
                           f"DID {f.did[0]:02X} {f.did[1]:02X} as module "
                           f"{f.tx:03X}/{f.rx:03X}")
        payload = self._payload(f)
        fld = concrete_field(f.req.field, len(payload))
        lo, hi = field_range(fld, len(payload))
        lo, hi = max(lo, _SPIN_MIN), min(hi, _SPIN_MAX)
        is_enum = f.signal.unit == "Enum"
        self._form.setRowVisible(self.value_spin, not is_enum)
        self._form.setRowVisible(self.enum_combo, is_enum)
        for sp in (self.raw_spin, self.sweep_from, self.sweep_to):
            sp.blockSignals(True)
            sp.setRange(lo, hi)
            sp.blockSignals(False)
        self.sweep_from.setValue(max(lo, 0))
        self.sweep_to.setValue(min(hi, max(lo, 0) + 255))
        self._reset_sweep()
        scale = abs(f.req.scale) or 1.0
        places = max(unit_decimals(f.signal.unit, True),
                     min(6, max(0, -math.floor(math.log10(scale)))))
        self.value_spin.setDecimals(places)
        unit = unit_label(f.signal.unit, True)
        self.value_spin.setSuffix(f" {unit}" if unit else "")
        self.enum_combo.clear()
        for e in f.signal.enum:
            self.enum_combo.addItem(f"{e.value} — {e.label}", userData=e.value)
        self._sync_from_payload()

    def _sync_from_payload(self) -> None:
        """Every control from the payload being sent now."""
        f = self._current
        if f is None:
            return
        payload = self._payload(f)
        raw = decode_field(payload, f.req.field)
        value = f.req.decode(payload)
        fld = concrete_field(f.req.field, len(payload))
        self.field_label.setText(
            ("auto: " if f.req.field.is_auto else "") + fld.describe()
            + f"   offset {f.req.offset:g}, scale {f.req.scale:g}")
        for w in (self.value_spin, self.raw_spin):
            w.blockSignals(True)
        if raw is not None:
            self.raw_spin.setValue(int(raw))
        if value is not None:
            self.value_spin.setValue(value)
        for w in (self.value_spin, self.raw_spin):
            w.blockSignals(False)
        if value is not None:
            idx = self.enum_combo.findData(int(round(value)))
            if idx >= 0:
                self.enum_combo.setCurrentIndex(idx)
        if not self.payload_edit.hasFocus():
            self.payload_edit.setText(payload.hex(" "))
        self.guess_label.setVisible(f.guessed)
        self.shows.setText("—" if value is None else f.signal.format_value(value))
        self._refresh_table()

    def _send_payload(self, payload: bytes, edited: bool = False) -> None:
        f = self._current
        if f is None:
            return
        old = self._payload(f)
        if edited and len(payload) != len(old):
            for g in self._faked:  # a length typed in is no longer a guess
                if (g.tx, g.did) == (f.tx, f.did):
                    g.guessed = False
        self._edited.add((f.tx, f.did))
        self.fake.set_override(f.tx, f.rx, f.did, payload)
        self._sync_from_payload()

    def _send_raw(self, raw: int) -> None:
        f = self._current
        if f is None:
            return
        payload = self._payload(f)
        self._send_payload(encode_field(payload, f.req.field, raw))

    def _on_payload_edited(self) -> None:
        try:
            payload = bytes.fromhex(self.payload_edit.text())
        except ValueError:
            QMessageBox.warning(self, "Payload", "That isn't hex bytes.")
            self._sync_from_payload()
            return
        self._send_payload(payload, edited=True)

    def _on_value(self, value: float) -> None:
        if self._current is not None:
            self._send_raw(self._current.req.raw_for(value))

    def _on_raw(self, raw: int) -> None:
        self._send_raw(raw)

    def _on_enum(self, index: int) -> None:
        f = self._current
        value = self.enum_combo.itemData(index)
        if f is not None and value is not None:
            self._send_raw(f.req.raw_for(value))

    # --- sweep ---

    def _reset_sweep(self) -> None:
        self._sweep = None

    def _step(self, direction: int) -> None:
        if self._current is None:
            return
        if self._sweep is None:
            self._sweep = Sweep(self.sweep_from.value(), self.sweep_to.value(),
                                self.sweep_step.value())
            raw = self._sweep.advance(direction)
        else:
            raw = self._sweep.advance(direction)
        self._send_raw(raw)

    def _on_auto(self, on: bool) -> None:
        self._auto_next = time.monotonic() if on else None

    # --- points ---

    def _add_point(self) -> None:
        f = self._current
        text = self.phone_value.text().strip()
        if f is None or not text:
            return
        payload = self._payload(f)
        if f.signal.unit == "Enum":
            value, label = None, text
        else:
            try:
                value, label = float(text), ""
            except ValueError:
                QMessageBox.warning(self, "Add Point",
                                    f"{text!r} isn't a number (in the signal's "
                                    f"metric unit).")
                return
        raw = decode_field(payload, f.req.field)
        f.signal.points.append(Point(
            payload.hex(), value, label,
            note=f"fake car, raw {raw:g}" if raw is not None else "fake car",
            source="emulator", at=time.time()))
        self.catalog.save()
        self.phone_value.clear()
        self._sync_from_payload()
        self._refresh_points()

    def _remove_point(self) -> None:
        f = self._current
        rows = sorted({i.row() for i in self.points.selectedIndexes()}, reverse=True)
        if f is None or not rows:
            return
        for row in rows:
            if row < len(f.signal.points):
                del f.signal.points[row]
        self.catalog.save()
        self._refresh_points()

    def _refresh_points(self) -> None:
        """The selected signal's points, and the best field and scale (or
        state mapping) the search finds for them."""
        f = self._current
        pts = f.signal.points if f is not None else []
        enum = f is not None and f.signal.unit == "Enum"
        found = (search_enum_fields(pts) if enum else search_fields(pts)) if pts else []
        best = self._best = found[0] if found else None
        fld = best.field if best else (f.req.field if f else None)

        tol = display_tolerance([p.value for p in pts if p.value is not None])
        self.points.setRowCount(len(pts))
        for row, p in enumerate(pts):
            raw = decode_field(p.payload_bytes, fld) if fld else None
            phone = p.label if enum else ("" if p.value is None else f"{p.value:g}")
            fitted, color = "", TEXT_DIM
            if best is not None and raw is not None:
                if enum:
                    got = next((e.label for e in best.mapping.entries
                                if e.value == int(raw)), "")
                    fitted, color = got, GREEN if got == p.label else RED
                elif p.value is not None:
                    v = (raw + best.fit.offset) * best.fit.scale
                    fitted = f"{v:.6g}"
                    color = GREEN if abs(v - p.value) <= tol else RED
            cells = (p.payload_bytes.hex(" "), "" if raw is None else f"{raw:g}",
                     phone, fitted)
            for col, text in enumerate(cells):
                item = QTableWidgetItem(text)
                if col in (0, 1):
                    item.setFont(MONO_FONT)
                if col == 3:
                    item.setForeground(QColor(color))
                self.points.setItem(row, col, item)

        if f is None:
            text, good = "", False
        elif best is None:
            need = "two different states" if enum else "2 points (3 to confirm)"
            text, good = f"Add {need} to find a fit.", False
        elif enum:
            m = best.mapping
            text = f"{best.field.describe()}: {m.summary()}"
            good = m.ok
        else:
            fit = best.fit
            text = (f"{best.field.describe()}, scale {fit.scale:g}, "
                    f"offset {fit.offset:g} — {fit.summary()}")
            good = fit.confirmed
        in_use = best is not None and f is not None and self._fit_in_use(f, best)
        if in_use:
            text += " (in use)"
        self.fit_label.setText(text)
        self.fit_label.setStyleSheet(f"color: {GREEN if good else AMBER};")
        self.use_fit.setEnabled(best is not None and not in_use and (
            best.mapping.ok if enum else best.fit.ok))

    @staticmethod
    def _fit_in_use(f: _Faked, best: Candidate) -> bool:
        req = f.req
        if req.field != best.field:
            return False
        if best.mapping is not None:
            have = {(e.value, e.label) for e in f.signal.enum}
            return all((e.value, e.label) in have for e in best.mapping.entries)
        return (math.isclose(req.offset, best.fit.offset, abs_tol=1e-9)
                and math.isclose(req.scale, best.fit.scale, rel_tol=1e-9))

    def _use_fit(self) -> None:
        f, best = self._current, self._best
        if f is None or best is None:
            return
        if best.fit is not None and not best.fit.exact and QMessageBox.question(
            self, "Use This Fit",
            f"The points don't all sit on this line ({best.fit.summary()}). "
            f"Use it anyway?") != QMessageBox.Yes:
            return
        req = f.req
        req.field = best.field
        if best.mapping is not None:
            fitted = {e.value for e in best.mapping.entries}
            f.signal.enum = sorted(
                [e for e in f.signal.enum if e.value not in fitted]
                + list(best.mapping.entries), key=lambda e: e.value)
        else:
            req.offset, req.scale = best.fit.offset, best.fit.scale
        self.catalog.save()
        if f.signal.in_firmware:
            from .gui import _gen_catalog_error, _run_gen_catalog  # noqa: PLC0415 -- gui imports this module
            try:
                _run_gen_catalog()
            except (subprocess.CalledProcessError, OSError) as e:
                QMessageBox.warning(self, "Use This Fit",
                                    "Saved, but gen_catalog.py failed:\n\n"
                                    + _gen_catalog_error(e))
        self.window.on_registry_changed()
        self.window.registry_tab.refresh()
        self._select(self.table.currentRow())  # the field's range may differ now

    # --- log ---

    def _clear_log(self) -> None:
        self.fake.clear_log()
        self._asked_total = -1

    def update_view(self) -> None:
        """Called on the window's timer."""
        if self._auto_next is not None and time.monotonic() >= self._auto_next:
            self._step(1)
            if self._sweep is not None and self._sweep.done:
                self.auto.setChecked(False)
            else:
                self._auto_next = time.monotonic() + self.interval.value()
        self._show_state()
        asked = self.fake.asked_list()
        total = sum(a.count for a in asked)
        if total == self._asked_total:
            return
        self._asked_total = total
        asked.sort(key=lambda a: a.last, reverse=True)
        self.asked.setRowCount(len(asked))
        for row, a in enumerate(asked):
            vals = [f"{a.tx:03X}", a.request.hex(" "), str(a.count), a.how]
            for col, text in enumerate(vals):
                item = _cell(text, mono=col < 3)
                if col == 3:
                    good = a.how in ("faked", "recording", "default")
                    item.setForeground(QColor(GREEN if good else AMBER))
                self.asked.setItem(row, col, item)
