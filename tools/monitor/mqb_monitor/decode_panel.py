"""The signal editor's decode side: which bytes of the payload are the
signal's (the byte strip), the raw -> true-value points kept with it, and
the solver (solver.py) run over those points.

Nothing here writes the registry -- SignalEditorDialog reads field(),
points() and samples() back when it saves, and applies a fit only through
the fitLinear / fitEnum signals, which it connects to its own offset/scale
and enum table.
"""
from __future__ import annotations

import time
from typing import Callable

from PySide6.QtCore import QRect, QSize, Qt, QTimer
from PySide6.QtCore import Signal as QtSignal
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtWidgets import (
    QCheckBox,
    QDialog,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from .catalog import (
    AUTO_FIELD,
    EnumEntry,
    Field,
    Point,
    decode_field,
    unit_label,
)
from .session import MAX_DISTINCT_SAMPLES
from .solver import (
    CONFIRM_POINTS,
    Candidate,
    display_tolerance,
    fit_enum,
    fit_points,
    search_enum_fields,
    search_fields,
)
from .theme import ACCENT, AMBER, BORDER, GREEN, MONO_FONT, PANEL_ALT, RED, TEXT, TEXT_DIM

CELL_W, CELL_H = 30, 30
MAX_FIELD_BYTES = 4

# Newest payload for this signal's DID right now: (payload, monotonic
# timestamp, response id), or None.
LiveFn = Callable[[], "tuple[bytes, float, int] | None"]


def _auto_span(payload: bytes) -> tuple[int, int]:
    """The bytes the legacy auto decode reads from `payload`."""
    return (0, 2) if len(payload) >= 2 else (0, len(payload))


class ByteStrip(QWidget):
    """The newest payload as byte cells. Bytes that differ across the
    samples are tinted amber; the field's bytes are outlined in the accent
    colour. Click a byte, or drag across up to four, to pick the field."""

    rangeSelected = QtSignal(int, int)  # start, length

    def __init__(self) -> None:
        super().__init__()
        self._payload = b""
        self._varying: set[int] = set()
        self._span = (0, 0)
        self._drag_from: int | None = None
        self.setCursor(Qt.PointingHandCursor)
        self.setToolTip("Click a byte, or drag across up to 4, to pick the "
                        "bytes this signal reads. Amber bytes changed "
                        "between samples.")

    def set_payloads(self, payloads: list[bytes]) -> None:
        self._payload = payloads[-1] if payloads else b""
        n = max((len(p) for p in payloads), default=0)
        self._varying = {
            i for i in range(n)
            if len({p[i] for p in payloads if len(p) > i}) > 1
        }
        self.updateGeometry()
        self.update()

    def set_span(self, start: int, length: int) -> None:
        self._span = (start, length)
        self.update()

    def sizeHint(self) -> QSize:  # noqa: N802 -- Qt override
        return QSize(max(len(self._payload), 8) * CELL_W + 2, CELL_H + 16)

    def minimumSizeHint(self) -> QSize:  # noqa: N802 -- Qt override
        return QSize(8 * CELL_W, CELL_H + 16)

    def _index_at(self, x: float) -> int | None:
        i = int(x // CELL_W)
        return i if 0 <= i < len(self._payload) else None

    def _select(self, a: int, b: int) -> None:
        lo, hi = min(a, b), max(a, b)
        if b < a:
            lo = max(lo, hi - MAX_FIELD_BYTES + 1)
        else:
            hi = min(hi, lo + MAX_FIELD_BYTES - 1)
        self.rangeSelected.emit(lo, hi - lo + 1)

    def mousePressEvent(self, event) -> None:  # noqa: N802 -- Qt override
        i = self._index_at(event.position().x())
        if i is not None:
            self._drag_from = i
            self._select(i, i)

    def mouseMoveEvent(self, event) -> None:  # noqa: N802 -- Qt override
        if self._drag_from is None:
            return
        x = min(max(event.position().x(), 0), len(self._payload) * CELL_W - 1)
        i = self._index_at(x)
        if i is not None:
            self._select(self._drag_from, i)

    def mouseReleaseEvent(self, event) -> None:  # noqa: N802 -- Qt override
        self._drag_from = None

    def paintEvent(self, event) -> None:  # noqa: N802 -- Qt override
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        if not self._payload:
            p.setPen(QColor(TEXT_DIM))
            p.drawText(self.rect(), Qt.AlignLeft | Qt.AlignVCenter,
                       "No samples yet")
            return
        start, length = self._span
        small = p.font()
        small.setPointSize(max(small.pointSize() - 3, 7))
        for i, b in enumerate(self._payload):
            cell = QRect(i * CELL_W + 1, 1, CELL_W - 3, CELL_H - 2)
            selected = start <= i < start + length
            p.setBrush(QColor(PANEL_ALT))
            p.setPen(QPen(QColor(ACCENT if selected else BORDER),
                          2 if selected else 1))
            p.drawRoundedRect(cell, 4, 4)
            if i in self._varying:
                tint = QColor(AMBER)
                tint.setAlpha(60)
                p.setBrush(tint)
                p.drawRoundedRect(cell, 4, 4)
            p.setFont(MONO_FONT)
            p.setPen(QColor(TEXT))
            p.drawText(cell, Qt.AlignCenter, f"{b:02X}")
            p.setFont(small)
            p.setPen(QColor(TEXT_DIM))
            p.drawText(QRect(i * CELL_W, CELL_H, CELL_W, 14), Qt.AlignCenter,
                       str(i))


# Points table columns.
COL_PAYLOAD, COL_RAW, COL_VALUE, COL_FITTED, COL_ERROR, COL_NOTE, COL_SOURCE = range(7)
EDITABLE = (COL_PAYLOAD, COL_VALUE, COL_NOTE)


class FieldSearchDialog(QDialog):
    """The field search's best candidates; the chosen one is `chosen`."""

    def __init__(self, found: list[Candidate], enum: bool, n_points: int,
                 parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Find field")
        self.setMinimumWidth(720)
        self.chosen: Candidate | None = None
        self._found = found[:15]

        layout = QVBoxLayout(self)
        if not enum and n_points < CONFIRM_POINTS:
            warn = QLabel(
                "Only 2 points: any field fits two points exactly, so this "
                "order is a guess (tidiest scale first). Add a third point "
                "to tell them apart.")
            warn.setWordWrap(True)
            warn.setStyleSheet(f"color: {AMBER};")
            layout.addWidget(warn)

        cols = ["Field", "Result", "Mapping" if enum else "Offset / scale",
                "Same result"]
        self.table = QTableWidget(len(self._found), len(cols))
        self.table.setHorizontalHeaderLabels(cols)
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.setSelectionMode(QTableWidget.SingleSelection)
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.Stretch)
        self.table.doubleClicked.connect(lambda *_: self._use())
        for row, c in enumerate(self._found):
            if enum:
                good = c.mapping.ok and c.mapping.extra_values == 0
                result = ("consistent" if c.mapping.ok
                          else f"{len(c.mapping.conflicts)} conflict(s)")
                detail = ", ".join(f"{e.value}={e.label}" for e in c.mapping.entries)
            else:
                good = c.fit.exact
                result = c.fit.summary()
                detail = f"{c.fit.offset:g} / {c.fit.scale:g}"
            vals = [c.field.describe(), result, detail,
                    ", ".join(f.describe() for f in c.also[:3])]
            for col, text in enumerate(vals):
                item = QTableWidgetItem(text)
                item.setToolTip(text)
                if col != 1:
                    item.setFont(MONO_FONT)
                if col == 1:
                    item.setForeground(QColor(GREEN if good else AMBER))
                self.table.setItem(row, col, item)
        self.table.resizeColumnToContents(0)
        if self._found:
            self.table.selectRow(0)
        layout.addWidget(self.table)

        btns = QHBoxLayout()
        btns.addStretch(1)
        cancel = QPushButton("Cancel")
        cancel.clicked.connect(self.reject)
        use = QPushButton("Use Selected")
        use.clicked.connect(self._use)
        btns.addWidget(cancel)
        btns.addWidget(use)
        layout.addLayout(btns)

    def _use(self) -> None:
        row = self.table.currentRow()
        if 0 <= row < len(self._found):
            self.chosen = self._found[row]
            self.accept()


class DecodePanel(QWidget):
    fieldChanged = QtSignal()
    samplesChanged = QtSignal()
    fitLinear = QtSignal(float, float)  # offset, scale
    fitEnum = QtSignal(list)  # list[EnumEntry]

    def __init__(
        self,
        samples: list[bytes],
        fld: Field,
        points: list[Point],
        live: LiveFn | None = None,
        others: list[tuple[str, Field]] | None = None,
        editable_field: bool = True,
    ) -> None:
        super().__init__()
        self._samples = list(samples)
        self._field = fld
        self._live = live
        self._live_payload: bytes | None = None
        self._live_age: float | None = None
        self._offset, self._scale = 0.0, 1.0
        self._unit = "None"
        self._enum: list[EnumEntry] = []

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        layout.addWidget(self._heading("Payload bytes"))
        self.strip = ByteStrip()
        self.strip.rangeSelected.connect(self._on_range)
        layout.addWidget(self.strip)

        frow = QHBoxLayout()
        self.field_label = QLabel()
        self.field_label.setFont(MONO_FONT)
        frow.addWidget(self.field_label, stretch=1)
        self.signed = QCheckBox("Signed")
        self.signed.toggled.connect(self._on_controls)
        frow.addWidget(self.signed)
        self.little = QCheckBox("Little-endian")
        self.little.setToolTip("Low byte first. Most VW data is big-endian, "
                               "but some DIDs aren't.")
        self.little.toggled.connect(self._on_controls)
        frow.addWidget(self.little)
        frow.addWidget(QLabel("Bits from"))
        self.bit_shift = QSpinBox()
        self.bit_shift.setRange(0, 31)
        frow.addWidget(self.bit_shift)
        frow.addWidget(QLabel("count"))
        self.bit_len = QSpinBox()
        self.bit_len.setRange(0, 32)
        self.bit_len.setSpecialValueText("all")
        frow.addWidget(self.bit_len)
        for sp in (self.bit_shift, self.bit_len):
            sp.valueChanged.connect(self._on_controls)
        self.auto_btn = QPushButton("Auto")
        self.auto_btn.setToolTip("The legacy decode: u8 for a 1-byte payload, "
                                 "else s16 from bytes 0-1.")
        self.auto_btn.clicked.connect(lambda: self._set_field(AUTO_FIELD))
        frow.addWidget(self.auto_btn)
        layout.addLayout(frow)
        self._field_widgets = (self.strip, self.signed, self.little, self.bit_shift,
                               self.bit_len, self.auto_btn)
        for w in self._field_widgets:
            w.setEnabled(editable_field)

        if others:
            shared = QLabel("Also read from this DID: " + "; ".join(
                f"{name} ({f.describe()})" for name, f in others))
            shared.setWordWrap(True)
            shared.setStyleSheet(f"color: {TEXT_DIM};")
            layout.addWidget(shared)

        self.live_label = QLabel()
        self.live_label.setFont(MONO_FONT)
        self.live_label.setVisible(live is not None)
        layout.addWidget(self.live_label)

        layout.addWidget(self._heading("Points"))
        self.table = QTableWidget(0, 7)
        self.table.setHorizontalHeaderLabels(
            ["Payload", "Raw", "Value", "Fitted", "Error", "Note", "Source"])
        self.table.verticalHeader().setVisible(False)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        hdr = self.table.horizontalHeader()
        hdr.setSectionResizeMode(COL_NOTE, QHeaderView.Stretch)
        for col in (COL_RAW, COL_VALUE, COL_FITTED, COL_ERROR, COL_SOURCE):
            hdr.setSectionResizeMode(col, QHeaderView.ResizeToContents)
        self.table.setColumnWidth(COL_PAYLOAD, 150)
        self.table.setMinimumHeight(170)
        self.table.itemChanged.connect(self._on_item_changed)
        layout.addWidget(self.table, stretch=1)

        brow = QHBoxLayout()
        self.live_btn = QPushButton("Add from Live")
        self.live_btn.setToolTip("Freeze the payload on the bus right now as "
                                 "a new point, then type the value the phone "
                                 "shows.")
        self.live_btn.clicked.connect(self._add_live)
        self.live_btn.setVisible(live is not None)
        brow.addWidget(self.live_btn)
        add_btn = QPushButton("Add Row")
        add_btn.clicked.connect(self._add_manual)
        brow.addWidget(add_btn)
        rm_btn = QPushButton("Remove")
        rm_btn.clicked.connect(self._remove_rows)
        brow.addWidget(rm_btn)
        brow.addStretch(1)
        self.fit_btn = QPushButton("Fit")
        self.fit_btn.setToolTip("Work out offset/scale (or, for Enum, the "
                                "value -> label table) from every point.")
        self.fit_btn.clicked.connect(self._fit)
        brow.addWidget(self.fit_btn)
        self.find_btn = QPushButton("Find Field…")
        self.find_btn.setToolTip("Try every byte range in the payload and "
                                 "rank them by how well they match the points.")
        self.find_btn.clicked.connect(self._find_field)
        self.find_btn.setEnabled(editable_field)
        brow.addWidget(self.find_btn)
        layout.addLayout(brow)

        self.result = QLabel()
        self.result.setWordWrap(True)
        layout.addWidget(self.result)

        for pt in points:
            self._append_row(pt)
        self.strip.set_payloads(self._samples)
        self._sync_field_ui()

        if live is not None:
            self._timer = QTimer(self)
            self._timer.timeout.connect(self._poll_live)
            self._timer.start(250)
            self._poll_live()

    @staticmethod
    def _heading(text: str) -> QLabel:
        label = QLabel(text)
        label.setStyleSheet(f"color: {TEXT_DIM}; font-weight: 600;")
        return label

    # --- what the dialog reads back ---

    def field(self) -> Field:
        return self._field

    def samples(self) -> list[bytes]:
        return list(self._samples)

    def newest(self) -> bytes:
        return self._samples[-1] if self._samples else b""

    def points(self) -> tuple[list[Point], str | None]:
        """The table's rows as points, or an error message. Fully blank rows
        are skipped."""
        out = []
        for row in range(self.table.rowCount()):
            payload_text = self._text(row, COL_PAYLOAD)
            value_text = self._text(row, COL_VALUE)
            note = self._text(row, COL_NOTE)
            if not (payload_text or value_text or note):
                continue
            try:
                payload = bytes.fromhex(payload_text)
            except ValueError:
                return [], f"Point {row + 1}: {payload_text!r} isn't hex bytes."
            if not payload:
                return [], f"Point {row + 1} has no payload."
            if not value_text:
                return [], f"Point {row + 1} has no value — type what the phone showed."
            old: Point = self.table.item(row, COL_PAYLOAD).data(Qt.UserRole)
            if self._is_enum():
                value, label = None, value_text
            else:
                try:
                    value, label = float(value_text), ""
                except ValueError:
                    return [], (f"Point {row + 1}: {value_text!r} isn't a number "
                                f"(for a state like Open/Closed, set Unit to Enum).")
            out.append(Point(payload.hex(), value, label, note,
                             old.source if old else "manual",
                             old.at if old else time.time()))
        return out, None

    # --- what the dialog pushes in ---

    def set_decode(self, offset: float, scale: float, unit: str,
                   enum: list[EnumEntry]) -> None:
        self._offset, self._scale = offset, scale
        self._unit, self._enum = unit, enum
        self.table.horizontalHeaderItem(COL_VALUE).setText(
            "Label" if self._is_enum()
            else f"Value ({unit_label(unit, True)})" if unit_label(unit, True)
            else "Value")
        self._refresh_computed()
        self._show_live()

    # --- field ---

    def _is_enum(self) -> bool:
        return self._unit == "Enum"

    def _on_range(self, start: int, length: int) -> None:
        keep = not self._field.is_auto
        self._set_field(Field(start, length, self.signed.isChecked() and keep,
                              little_endian=self.little.isChecked() and keep))

    def _on_controls(self) -> None:
        if self._field.is_auto:
            return
        bit_len = self.bit_len.value()
        self._set_field(Field(
            self._field.start, self._field.length, self.signed.isChecked(),
            self.bit_shift.value() if bit_len else 0, bit_len,
            little_endian=self.little.isChecked()))

    def _set_field(self, fld: Field) -> None:
        if fld == self._field:
            return
        self._field = fld
        self._sync_field_ui()
        self.fieldChanged.emit()

    def _sync_field_ui(self) -> None:
        fld = self._field
        newest = self.newest()
        for w in (self.signed, self.little, self.bit_shift, self.bit_len):
            w.blockSignals(True)
        self.signed.setChecked(fld.signed)
        self.little.setChecked(fld.little_endian)
        self.bit_shift.setValue(fld.bit_shift)
        self.bit_len.setValue(fld.bit_len)
        for w in (self.signed, self.little, self.bit_shift, self.bit_len):
            w.blockSignals(False)
            w.setEnabled(not fld.is_auto and self.strip.isEnabled())
        self.bit_shift.setEnabled(self.bit_len.isEnabled() and fld.bit_len > 0)
        self.little.setEnabled(self.little.isEnabled() and fld.length > 1)
        if fld.is_auto:
            self.strip.set_span(*_auto_span(newest))
            kind = "s16 @ byte 0" if len(newest) >= 2 else "u8"
            text = f"auto ({kind})" if newest else "auto"
        else:
            self.strip.set_span(fld.start, fld.length)
            text = fld.describe()
        raw = decode_field(newest, fld) if newest else None
        if raw is not None:
            text += f"  →  raw {raw:g}"
        elif newest:
            text += "  →  past the end of the payload"
        self.field_label.setText(text)
        self._refresh_computed()
        self._show_live()

    # --- live ---

    def _poll_live(self) -> None:
        got = self._live() if self._live else None
        self._live_payload = got[0] if got else None
        self._live_age = time.monotonic() - got[1] if got else None
        self.live_btn.setEnabled(got is not None)
        if got and got[0] != self.newest():
            # Keep every distinct payload seen while the editor is open.
            if got[0] in self._samples:
                self._samples.remove(got[0])
            self._samples.append(got[0])
            del self._samples[:-MAX_DISTINCT_SAMPLES]
            self.strip.set_payloads(self._samples)
            self._sync_field_ui()
            self.samplesChanged.emit()
        self._show_live()

    def _show_live(self) -> None:
        if self._live is None:
            return
        payload = self._live_payload
        if payload is None:
            self.live_label.setText("Live: not answering")
            self.live_label.setStyleSheet(f"color: {TEXT_DIM};")
            return
        raw = decode_field(payload, self._field)
        text = f"Live: [{payload.hex(' ')}]"
        if raw is not None:
            text += f"  raw {raw:g}  →  {self._format(raw)}"
        self.live_label.setText(f"{text}  ({self._live_age:.1f} s ago)")
        self.live_label.setStyleSheet(f"color: {GREEN if self._live_age < 2 else TEXT_DIM};")

    def _format(self, raw: float) -> str:
        v = (raw + self._offset) * self._scale
        if self._is_enum():
            n = int(round(v))
            return next((e.label for e in self._enum if e.value == n), f"?{n}")
        unit = unit_label(self._unit, True)
        return f"{v:.4g} {unit}".strip()

    # --- points table ---

    def _text(self, row: int, col: int) -> str:
        item = self.table.item(row, col)
        return item.text().strip() if item else ""

    def _append_row(self, pt: Point | None, payload: bytes = b"",
                    source: str = "manual") -> int:
        self.table.blockSignals(True)
        row = self.table.rowCount()
        self.table.insertRow(row)
        if pt is None:
            pt = Point(payload.hex(), None, source=source, at=time.time())
        value_text = pt.label or ("" if pt.value is None else f"{pt.value:g}")
        texts = {COL_PAYLOAD: bytes.fromhex(pt.payload).hex(" "),
                 COL_VALUE: value_text, COL_NOTE: pt.note,
                 COL_SOURCE: pt.source}
        for col in range(self.table.columnCount()):
            item = QTableWidgetItem(texts.get(col, ""))
            if col == COL_NOTE:
                item.setToolTip(pt.note)
            if col in (COL_PAYLOAD, COL_RAW, COL_FITTED, COL_ERROR):
                item.setFont(MONO_FONT)
            if col not in EDITABLE:
                item.setFlags(item.flags() & ~Qt.ItemIsEditable)
                item.setForeground(QColor(TEXT_DIM))
            self.table.setItem(row, col, item)
        self.table.item(row, COL_PAYLOAD).setData(Qt.UserRole, pt)
        self.table.blockSignals(False)
        self._refresh_computed()
        return row

    def _add_live(self) -> None:
        if self._live_payload is None:
            return
        row = self._append_row(None, self._live_payload, source="live")
        self._edit_value(row)

    def _add_manual(self) -> None:
        row = self._append_row(None, self.newest(), source="manual")
        self._edit_value(row)

    def _edit_value(self, row: int) -> None:
        self.table.setCurrentCell(row, COL_VALUE)
        self.table.editItem(self.table.item(row, COL_VALUE))

    def _remove_rows(self) -> None:
        for row in sorted({i.row() for i in self.table.selectedItems()}, reverse=True):
            self.table.removeRow(row)
        self._refresh_computed()

    def _on_item_changed(self, item: QTableWidgetItem) -> None:
        if item.column() in (COL_PAYLOAD, COL_VALUE):
            self._refresh_computed()

    def _refresh_computed(self) -> None:
        """Raw, Fitted and Error for every row, from the current field,
        offset/scale and enum table."""
        values = []
        for row in range(self.table.rowCount()):
            try:
                values.append(float(self._text(row, COL_VALUE)))
            except ValueError:
                pass
        tol = display_tolerance(values)
        self.table.blockSignals(True)
        for row in range(self.table.rowCount()):
            raw_text = fitted_text = err_text = ""
            err_color = TEXT_DIM
            try:
                payload = bytes.fromhex(self._text(row, COL_PAYLOAD))
            except ValueError:
                payload, raw_text = None, "bad hex"
            raw = decode_field(payload, self._field) if payload else None
            if raw is not None:
                raw_text = f"{raw:g}"
                fitted_text = self._format(raw)
                value_text = self._text(row, COL_VALUE)
                if self._is_enum() and value_text:
                    ok = fitted_text == value_text
                    err_text, err_color = ("✓", GREEN) if ok else ("✗", RED)
                elif value_text:
                    try:
                        err = float(value_text) - (raw + self._offset) * self._scale
                        err_text = f"{err:+.3g}"
                        err_color = GREEN if abs(err) <= tol else RED
                    except ValueError:
                        pass
            elif payload:
                raw_text = "too short"
            for col, text in ((COL_RAW, raw_text), (COL_FITTED, fitted_text),
                              (COL_ERROR, err_text)):
                self.table.item(row, col).setText(text)
            self.table.item(row, COL_ERROR).setForeground(QColor(err_color))
        self.table.blockSignals(False)

    # --- solver ---

    def _fit(self) -> None:
        points, err = self.points()
        if err:
            QMessageBox.warning(self, "Fit", err)
            return
        if self._is_enum():
            m = fit_enum(points, self._field)
            self._show_result(m.ok, m.summary())
            if m.ok:
                self.fitEnum.emit(m.entries)
            return
        fit = fit_points(points, self._field)
        self._show_result(fit.confirmed, fit.summary()
                          + ("" if fit.problem else
                             f" — offset {fit.offset:g}, scale {fit.scale:g}"))
        if fit.problem:
            return
        if not fit.exact and QMessageBox.question(
            self, "Fit",
            f"The points don't sit on a straight line through these bytes:\n"
            f"{fit.summary()}.\n\nUse the closest fit (offset {fit.offset:g}, "
            f"scale {fit.scale:g}) anyway?",
        ) != QMessageBox.Yes:
            return
        self.fitLinear.emit(fit.offset, fit.scale)

    def _find_field(self) -> None:
        points, err = self.points()
        if err:
            QMessageBox.warning(self, "Find field", err)
            return
        enum = self._is_enum()
        found = search_enum_fields(points) if enum else search_fields(points)
        if not found:
            need = ("points with at least two different labels" if enum
                    else "at least 2 points with a value")
            QMessageBox.information(self, "Find field", f"Needs {need}.")
            return
        n = sum(1 for p in points if (p.label if enum else p.value is not None))
        dlg = FieldSearchDialog(found, enum, n, parent=self)
        if dlg.exec() != QDialog.Accepted or dlg.chosen is None:
            return
        c = dlg.chosen
        self._set_field(c.field)
        if enum:
            self._show_result(c.mapping.ok, c.mapping.summary())
            if c.mapping.ok:
                self.fitEnum.emit(c.mapping.entries)
        else:
            self._show_result(c.fit.confirmed, c.fit.summary()
                              + f" — offset {c.fit.offset:g}, scale {c.fit.scale:g}")
            self.fitLinear.emit(c.fit.offset, c.fit.scale)

    def _show_result(self, good: bool, text: str) -> None:
        self.result.setText(text)
        self.result.setStyleSheet(f"color: {GREEN if good else AMBER};")
