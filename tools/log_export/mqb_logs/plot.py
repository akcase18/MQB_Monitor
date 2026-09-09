"""The trace plot: a hand-painted, pannable/zoomable view of one session's
signals over time.

Painted with QPainter rather than pulled in from a charting library, for two
reasons. The tool's whole dependency set is PySide6 + pyserial (see the
README) and stays that way -- `tools/monitor`'s venv is shared, and adding
pyqtgraph/numpy to it for one page is a poor trade. And a charting library
would have to be argued out of doing the two things this data cannot have:

  * **Steps, not interpolation.** The firmware writes a record when a value
    CHANGES (core/logging), so a sample means "it became this, and stayed
    this until the next sample". A straight line between two samples draws
    values the ECU never reported -- worst on a signal that idles for a
    minute and then jumps.
  * **Per-signal time bases.** Every signal has its own irregular sample
    times; nothing is on a shared grid, so there is no "row" to plot and
    nothing to resample onto without inventing data.

Rows are grouped by the registry's unit *category*, so same-kind signals
(coolant vs. oil temp) share an axis and can be read against each other,
while RPM doesn't flatten a temperature trace into a line at the bottom.

Decimation: a session can hold six figures of records, which is far more
than there are pixels and far more than a Python paint loop can walk at
interactive speed. Each series therefore gets a min/max summary built once
(_Series), and a paint walks whichever source -- raw samples or summary
buckets -- keeps the drawn point count bounded, at a stride that keeps it
bounded regardless of zoom. Because buckets carry the true min and max of
their span, zoomed out you still see every spike; it just stops being
individually addressable.
"""
from __future__ import annotations

import bisect
import math
import sys
from pathlib import Path

from PySide6.QtCore import QPoint, QRect, Qt
from PySide6.QtGui import QColor, QFont, QFontMetrics, QPainter, QPainterPath, QPen
from PySide6.QtWidgets import QWidget

from .logfile import Trace
from .theme import BORDER, PANEL, SERIES_COLORS, TEXT, TEXT_DIM

# Same trick, and the same reason, as theme.py's: reach tools/monitor without
# either tool being an installed package. unit_label/unit_decimals are that
# module's mirror of the firmware's signal_catalog.cpp helpers, and a third
# copy of the unit table is exactly the kind of thing that drifts.
_MONITOR_DIR = Path(__file__).resolve().parents[2] / "monitor"
if str(_MONITOR_DIR) not in sys.path:
    sys.path.insert(0, str(_MONITOR_DIR))

from mqb_monitor.catalog import unit_decimals, unit_label  # noqa: E402

LEFT_GUTTER = 68  # y tick labels
RIGHT_PAD = 14
TOP_PAD = 8
AXIS_HEIGHT = 22  # the shared x axis, drawn once under the bottom row
ROW_GAP = 12
ROW_HEADER = 17  # unit caption + legend, inside the row above its plot area
MIN_ROW_HEIGHT = 74  # below this a row can't be read; the page scrolls instead

# Drawn points per series per paint, as a multiple of the plot's pixel
# width: past a couple of points per pixel nothing is added that anyone
# can see, and the stride exists to stop paying for it.
POINTS_PER_PIXEL = 2
MIN_DRAW_POINTS = 800  # ...but never decimate a narrow window to mush
SUMMARY_BUCKETS = 20000  # size of a series' min/max summary, when it needs one

# DO NOT RAISE THIS ABOVE 1.0 to make the traces read heavier. Qt's raster
# engine draws a <=1px pen as cosmetic lines, and anything wider through its
# general stroker, which builds an outline polygon for the whole path. The
# cliff is absolute rather than gradual -- measured on one 7,200-point trace,
# antialiased, offscreen:
#
#     width 1.0 -> 4 ms        width 1.2 -> 796 ms      width 1.4 -> 816 ms
#
# At six traces a paint that is the difference between panning smoothly and
# freezing for seconds a frame. If the traces ever genuinely need more
# weight, the way to get it is a second offset pass at width 1.0, or drawing
# into a QImage once per view change instead of per paint -- not this number.
TRACE_PEN_WIDTH = 1.0

MIN_SPAN_S = 0.05  # tightest zoom, so a scroll can't collapse the view to 0
ZOOM_PER_NOTCH = 1.25


def _nice_step(span: float, target: int) -> float:
    """A 1/2/5 x 10^n gridline step giving roughly `target` divisions across
    `span` -- the usual axis-tick heuristic, so ticks land on numbers a
    person would have picked."""
    if span <= 0 or target <= 0:
        return 1.0
    rough = span / target
    magnitude = 10.0 ** math.floor(math.log10(rough))
    for multiple in (1.0, 2.0, 5.0, 10.0):
        if rough <= multiple * magnitude:
            return multiple * magnitude
    return 10.0 * magnitude


def _decimals_for(step: float) -> int:
    """Enough decimals to tell two adjacent ticks apart, and no more."""
    if step <= 0:
        return 0
    return max(0, min(3, int(math.ceil(-math.log10(step)))))


def _format_time(seconds: float, span: float) -> str:
    """Seconds from the session's start. Past two minutes that reads better
    as m:ss than as a four-digit second count."""
    if span >= 120:
        minutes, secs = divmod(int(seconds), 60)
        return f"{minutes}:{secs:02d}"
    decimals = 0 if span >= 20 else 1
    return f"{seconds:.{decimals}f}"


class _Series:
    """A Trace plus what the plot needs to draw it: its color, and a min/max
    summary for the zoom levels where walking every sample is too slow.

    The summary is four PARALLEL LISTS rather than a list of bucket objects,
    which is not stylistic: every hot path here is a min()/max() over a
    contiguous run of buckets, and over a plain list of floats that runs in
    C, while the same thing over objects is a Python-level attribute fetch
    per bucket. Measured on a 45-minute session (188k records, ~18k buckets
    a signal) that distinction was the difference between a ~240ms paint and
    a ~20ms one -- i.e. between panning and slideshow.

    The summary is built once, when the session is set. Nothing else here is
    allowed to be O(samples): a paint runs on every pan step."""

    def __init__(self, trace: Trace, color: str) -> None:
        self.trace = trace
        self.color = QColor(color)
        self.b_t: list[float] = []  # bucket start time
        self.b_lo: list[float] = []  # true min over the bucket's samples
        self.b_hi: list[float] = []  # true max
        self.b_last: list[float] = []  # last value, for joining to the next

        count = len(trace)
        if count <= SUMMARY_BUCKETS:
            return  # small enough that raw samples are always affordable

        step = math.ceil(count / SUMMARY_BUCKETS)
        times, values = trace.times_s, trace.values
        for start in range(0, count, step):
            window = values[start:start + step]
            self.b_t.append(times[start])
            self.b_lo.append(min(window))
            self.b_hi.append(max(window))
            self.b_last.append(window[-1])

    @property
    def decimated(self) -> bool:
        return bool(self.b_t)

    def window(self, t0: float, t1: float) -> tuple[int, int]:
        """Raw sample indices covering [t0, t1], plus the one sample before
        it: on-change records mean the value entering the view was set by a
        sample that may be far to the left of it."""
        times = self.trace.times_s
        lo = max(0, bisect.bisect_left(times, t0) - 1)
        hi = min(len(times), bisect.bisect_right(times, t1) + 1)
        return lo, hi

    def bucket_window(self, t0: float, t1: float) -> tuple[int, int]:
        lo = max(0, bisect.bisect_left(self.b_t, t0) - 1)
        hi = min(len(self.b_t), bisect.bisect_right(self.b_t, t1) + 1)
        return lo, hi

    def value_span(self, t0: float, t1: float) -> tuple[float, float] | None:
        """(min, max) over the visible window, or None if nothing is visible.

        Read off the summary when there is one -- a bucket's lo/hi are the
        true extremes of its span, so the answer is exact either way, and
        the axis can't clip a spike it decided not to draw individually."""
        if self.decimated:
            lo, hi = self.bucket_window(t0, t1)
            if lo >= hi:
                return None
            return min(self.b_lo[lo:hi]), max(self.b_hi[lo:hi])

        lo, hi = self.window(t0, t1)
        if lo >= hi:
            return None
        visible = self.trace.values[lo:hi]
        return min(visible), max(visible)


class TracePlot(QWidget):
    """Stacked, unit-grouped rows sharing one x axis, with a crosshair whose
    readout is the *held* value at the cursor (see Trace.value_at).

    Wheel zooms about the cursor, drag pans, double-click resets."""

    def __init__(self) -> None:
        super().__init__()
        self.setMouseTracking(True)
        self.setMinimumHeight(240)
        self.setFocusPolicy(Qt.StrongFocus)

        self._series: list[_Series] = []
        self._hidden: set[int] = set()  # signal_ids toggled off in the sidebar
        self._span_s = 0.0  # the full session, and so the reset target
        self._view = (0.0, 0.0)
        self._cursor: QPoint | None = None
        self._pan_from: tuple[int, float] | None = None

        self._label_font = QFont(self.font())
        self._label_font.setPointSize(9)

    # --- data -----------------------------------------------------------

    def set_traces(self, traces: list[Trace]) -> None:
        """Colors are assigned in the order given (build_traces sorts by
        name), so the same signal keeps its color between the legend, the
        checkbox list and the plot."""
        self._series = [
            _Series(t, SERIES_COLORS[i % len(SERIES_COLORS)])
            for i, t in enumerate(traces)
        ]
        self._hidden = set()
        self._span_s = max((t.times_s[-1] for t in traces if t.times_s), default=0.0)
        self.reset_view()

    def color_for(self, signal_id: int) -> str:
        for series in self._series:
            if series.trace.signal_id == signal_id:
                return series.color.name()
        return TEXT_DIM

    def set_hidden(self, signal_ids: set[int]) -> None:
        self._hidden = set(signal_ids)
        self._update_height()
        self.update()

    def _visible(self) -> list[_Series]:
        return [s for s in self._series if s.trace.signal_id not in self._hidden]

    # --- view -----------------------------------------------------------

    def reset_view(self) -> None:
        self._view = (0.0, max(self._span_s, MIN_SPAN_S))
        self._update_height()
        self.update()

    def _rows(self) -> list[tuple[str, list[_Series]]]:
        """Visible series grouped by unit category, in first-appearance
        order so a row doesn't jump around as signals are toggled."""
        rows: dict[str, list[_Series]] = {}
        for series in self._visible():
            rows.setdefault(series.trace.unit, []).append(series)
        return list(rows.items())

    def _update_height(self) -> None:
        """Rows never squeeze below readable; past that the page scrolls."""
        rows = max(1, len(self._rows()))
        needed = TOP_PAD + rows * (MIN_ROW_HEIGHT + ROW_GAP) + AXIS_HEIGHT
        self.setMinimumHeight(needed)

    def _plot_x_range(self) -> tuple[int, int]:
        return LEFT_GUTTER, max(LEFT_GUTTER + 1, self.width() - RIGHT_PAD)

    def _time_at_x(self, x: int) -> float:
        left, right = self._plot_x_range()
        t0, t1 = self._view
        fraction = (x - left) / max(1, right - left)
        return t0 + fraction * (t1 - t0)

    # --- interaction ----------------------------------------------------

    def wheelEvent(self, event) -> None:
        notches = event.angleDelta().y() / 120.0
        if not notches:
            return
        anchor = self._time_at_x(int(event.position().x()))
        t0, t1 = self._view
        factor = ZOOM_PER_NOTCH ** -notches  # scroll up = zoom in
        span = max(MIN_SPAN_S, min(self._span_s or MIN_SPAN_S, (t1 - t0) * factor))

        # Keep whatever is under the cursor under the cursor.
        share = (anchor - t0) / max(1e-9, t1 - t0)
        self._view = self._clamp(anchor - share * span, span)
        self.update()

    def mousePressEvent(self, event) -> None:
        if event.button() == Qt.LeftButton:
            self._pan_from = (int(event.position().x()), self._view[0])
            self.setCursor(Qt.ClosedHandCursor)

    def mouseMoveEvent(self, event) -> None:
        self._cursor = event.position().toPoint()
        if self._pan_from is not None:
            start_x, start_t0 = self._pan_from
            left, right = self._plot_x_range()
            t0, t1 = self._view
            per_pixel = (t1 - t0) / max(1, right - left)
            self._view = self._clamp(
                start_t0 - (int(event.position().x()) - start_x) * per_pixel, t1 - t0
            )
        self.update()

    def mouseReleaseEvent(self, event) -> None:
        if event.button() == Qt.LeftButton:
            self._pan_from = None
            self.unsetCursor()

    def mouseDoubleClickEvent(self, event) -> None:
        self.reset_view()

    def leaveEvent(self, event) -> None:
        self._cursor = None
        self.update()

    def _clamp(self, t0: float, span: float) -> tuple[float, float]:
        """Holds the view inside the session, so panning can't wander off
        into empty time on either side."""
        full = max(self._span_s, MIN_SPAN_S)
        span = min(span, full)
        t0 = max(0.0, min(t0, full - span))
        return t0, t0 + span

    # --- painting -------------------------------------------------------

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor(PANEL))
        painter.setFont(self._label_font)

        rows = self._rows()
        if not rows:
            painter.setPen(QColor(TEXT_DIM))
            painter.drawText(
                self.rect(), Qt.AlignCenter,
                "No signals selected."
                if self._series else "Select a session with records to plot.",
            )
            return

        painter.setRenderHint(QPainter.Antialiasing, True)
        left, right = self._plot_x_range()
        t0, t1 = self._view

        available = self.height() - TOP_PAD - AXIS_HEIGHT - ROW_GAP * len(rows)
        row_height = max(MIN_ROW_HEIGHT, available // len(rows))

        x_step = _nice_step(t1 - t0, max(3, (right - left) // 110))
        top = TOP_PAD
        for unit, series_list in rows:
            rect = QRect(left, top + ROW_HEADER, right - left, row_height - ROW_HEADER)
            self._paint_row(painter, rect, unit, series_list, x_step)
            top += row_height + ROW_GAP

        self._paint_x_axis(painter, left, right, top - ROW_GAP, x_step)
        if self._cursor is not None and left <= self._cursor.x() <= right:
            self._paint_crosshair(painter, TOP_PAD, top - ROW_GAP)

    def _paint_row(
        self,
        painter: QPainter,
        rect: QRect,
        unit: str,
        series_list: list[_Series],
        x_step: float,
    ) -> None:
        t0, t1 = self._view

        # Autoscale to what's actually on screen: zooming into a signal that
        # only moves half a degree should show that half degree, not a flat
        # line inside the session's full range.
        spans = [s.value_span(t0, t1) for s in series_list]
        present = [s for s in spans if s is not None]
        lo = min(s[0] for s in present) if present else 0.0
        hi = max(s[1] for s in present) if present else 1.0
        if hi - lo < 1e-9:
            lo, hi = lo - 1.0, hi + 1.0  # a constant signal still needs a band
        pad = (hi - lo) * 0.08
        lo, hi = lo - pad, hi + pad

        y_step = _nice_step(hi - lo, max(2, rect.height() // 34))
        self._paint_grid(painter, rect, lo, hi, y_step, x_step, unit)
        self._paint_header(painter, rect, unit, series_list)

        for series in series_list:
            self._paint_series(painter, rect, series, lo, hi)

        painter.setPen(QPen(QColor(BORDER), 1))
        painter.drawRect(rect)

    def _paint_grid(
        self,
        painter: QPainter,
        rect: QRect,
        lo: float,
        hi: float,
        y_step: float,
        x_step: float,
        unit: str,
    ) -> None:
        t0, t1 = self._view
        grid = QPen(QColor(BORDER), 1, Qt.DotLine)
        decimals = max(_decimals_for(y_step), unit_decimals(unit, True))

        painter.setPen(grid)
        tick = math.ceil(lo / y_step) * y_step
        y_labels: list[tuple[int, str]] = []
        while tick <= hi:
            y = rect.bottom() - int((tick - lo) / (hi - lo) * rect.height())
            painter.drawLine(rect.left(), y, rect.right(), y)
            y_labels.append((y, f"{tick:.{decimals}f}"))
            tick += y_step

        tick = math.ceil(t0 / x_step) * x_step
        while tick <= t1:
            x = rect.left() + int((tick - t0) / (t1 - t0) * rect.width())
            painter.drawLine(x, rect.top(), x, rect.bottom())
            tick += x_step

        painter.setPen(QColor(TEXT_DIM))
        for y, text in y_labels:
            painter.drawText(
                QRect(0, y - 7, LEFT_GUTTER - 6, 14),
                Qt.AlignRight | Qt.AlignVCenter,
                text,
            )

    def _paint_header(
        self, painter: QPainter, rect: QRect, unit: str, series_list: list[_Series]
    ) -> None:
        """The row caption: its unit, then a swatch + name per signal. With
        the crosshair up, each name carries the value held at the cursor --
        the legend doubles as the readout rather than costing a second
        panel."""
        metrics = QFontMetrics(self._label_font)
        y = rect.top() - ROW_HEADER
        x = rect.left()

        label = unit_label(unit, True) or unit
        painter.setPen(QColor(TEXT_DIM))
        painter.drawText(QRect(x, y, 60, ROW_HEADER), Qt.AlignLeft | Qt.AlignVCenter, label)
        x += metrics.horizontalAdvance(label) + 14

        at_time = (
            self._time_at_x(self._cursor.x())
            if self._cursor is not None and rect.left() <= self._cursor.x() <= rect.right()
            else None
        )
        decimals = unit_decimals(unit, True)

        for series in series_list:
            text = series.trace.name
            if at_time is not None:
                value = series.trace.value_at(at_time)
                text += f"  {value:.{decimals}f}" if value is not None else "  —"

            painter.fillRect(QRect(x, y + 6, 8, 3), series.color)
            x += 13
            painter.setPen(QColor(TEXT))
            painter.drawText(
                QRect(x, y, metrics.horizontalAdvance(text) + 4, ROW_HEADER),
                Qt.AlignLeft | Qt.AlignVCenter,
                text,
            )
            x += metrics.horizontalAdvance(text) + 18
            if x > rect.right():
                break  # more signals than the row is wide; the checkbox list
                # is where you'd turn some off

    def _paint_series(
        self, painter: QPainter, rect: QRect, series: _Series, lo: float, hi: float
    ) -> None:
        t0, t1 = self._view
        span = max(1e-9, t1 - t0)
        value_span = max(1e-9, hi - lo)

        def to_x(t: float) -> float:
            return rect.left() + (t - t0) / span * rect.width()

        def to_y(v: float) -> float:
            return rect.bottom() - (v - lo) / value_span * rect.height()

        painter.save()
        painter.setClipRect(rect)
        painter.setPen(QPen(series.color, TRACE_PEN_WIDTH))

        budget = max(MIN_DRAW_POINTS, rect.width() * POINTS_PER_PIXEL)
        raw_lo, raw_hi = series.window(t0, t1)
        if raw_hi - raw_lo <= budget or not series.decimated:
            self._paint_steps(painter, series, raw_lo, raw_hi, to_x, to_y)
        else:
            self._paint_envelope(painter, series, budget, to_x, to_y)
        painter.restore()

    def _paint_steps(self, painter, series, lo_i, hi_i, to_x, to_y) -> None:
        """Exact: every sample, held until the next one. What you get once
        the view is tight enough for the samples to be individually
        meaningful.

        One stroked path rather than a drawLine per segment -- thousands of
        individual calls is the difference between a plot that pans and one
        that stutters."""
        times = series.trace.times_s
        values = series.trace.values
        path = QPainterPath()
        previous_y = None
        for i in range(lo_i, hi_i):
            x, y = to_x(times[i]), to_y(values[i])
            if previous_y is None:
                path.moveTo(x, y)
            else:
                path.lineTo(x, previous_y)  # hold the old value across to here
                path.lineTo(x, y)  # then step to the new one
            previous_y = y
        if previous_y is not None:
            # The last value holds to the edge of the view -- on the device
            # it held until something changed it or the log ended.
            path.lineTo(to_x(self._view[1]), previous_y)
        painter.drawPath(path)

    def _paint_envelope(self, painter, series, budget, to_x, to_y) -> None:
        """Decimated: a min/max bar per bucket, joined so the line stays
        continuous. Bounded work per paint regardless of how far out the
        view is, and it still shows every spike -- a bucket's bar spans the
        true extremes of the samples it covers, so a transient can be
        drawn thinner than a pixel but never dropped."""
        t0, t1 = self._view
        lo_i, hi_i = series.bucket_window(t0, t1)
        stride = max(1, (hi_i - lo_i) // budget)
        b_t, b_lo, b_hi, b_last = series.b_t, series.b_lo, series.b_hi, series.b_last

        path = QPainterPath()
        previous_y = None
        for i in range(lo_i, hi_i, stride):
            end = min(i + stride, hi_i)
            x = to_x(b_t[i])
            bar_lo = to_y(min(b_lo[i:end]))
            bar_hi = to_y(max(b_hi[i:end]))
            if previous_y is None:
                path.moveTo(x, bar_lo)
            else:
                path.lineTo(x, previous_y)
            path.lineTo(x, bar_lo)
            path.lineTo(x, bar_hi)
            previous_y = to_y(b_last[end - 1])
            path.lineTo(x, previous_y)
        if previous_y is not None:
            path.lineTo(to_x(t1), previous_y)
        painter.drawPath(path)

    def _paint_x_axis(
        self, painter: QPainter, left: int, right: int, bottom: int, x_step: float
    ) -> None:
        t0, t1 = self._view
        metrics = QFontMetrics(self._label_font)
        caption = (
            "seconds from session start" if t1 - t0 < 120
            else "m:ss from session start"
        )
        # The caption owns the right end of the axis; ticks that would run
        # into it are dropped rather than drawn over it.
        caption_left = right - metrics.horizontalAdvance(caption)

        painter.setPen(QColor(TEXT_DIM))
        tick = math.ceil(t0 / x_step) * x_step
        while tick <= t1:
            text = _format_time(tick, t1 - t0)
            x = left + int((tick - t0) / (t1 - t0) * (right - left))
            if x + metrics.horizontalAdvance(text) / 2 + 10 < caption_left:
                painter.drawText(
                    QRect(x - 40, bottom + 3, 80, AXIS_HEIGHT - 4),
                    Qt.AlignHCenter | Qt.AlignTop,
                    text,
                )
            tick += x_step
        painter.drawText(
            QRect(left, bottom + 3, right - left, AXIS_HEIGHT - 4),
            Qt.AlignRight | Qt.AlignTop,
            caption,
        )

    def _paint_crosshair(self, painter: QPainter, top: int, bottom: int) -> None:
        x = self._cursor.x()
        painter.setPen(QPen(QColor(TEXT_DIM), 1, Qt.DashLine))
        painter.drawLine(x, top, x, bottom)
