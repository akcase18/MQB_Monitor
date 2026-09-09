"""PySide6 GUI for offloading the dashboard's black-box log.

Same shell as tools/monitor (shared theme, top bar + sidebar + stacked
pages), over a very different job: the monitor follows a live stream, this
pulls a finite blob once and then lets you look at it.

  * Device -- what the device says about its log (LOGSTATUS), and the
              actions: download, save the raw dump, erase.
  * Logs   -- what came back, grouped into sessions, with a record preview
              and CSV export.
  * Plot   -- one session's signals drawn against time (plot.py), for
              reading a drive rather than exporting it. Independent of the
              CSV path: neither needs the other, and the export writes the
              same rows whether or not anything was ever plotted.

Two sources feed the same decode path: the Teensy over USB, and a storage
*directory* -- the simulator's sim_storage/, or a copy of one -- which is
the equivalent of the monitor's Demo source for developing this app with no
hardware plugged in.

Sessions, not captures, are the grouping. A session is one power cycle
(session_id in every chunk header). A capture -- one Logging on -> off
cycle, what the user calls "a log" -- is finer, and a dump can't resolve
it: the capture index isn't part of the LOGDUMP stream, and even read off a
storage directory it only bounds captures to *chunks*, several of which can
share one chunk with nothing in the records marking the boundary. So the
capture count is reported (it's a true fact about the device) but nothing
is grouped by it.
"""
from __future__ import annotations

import time
from pathlib import Path

from PySide6.QtCore import Qt, QSettings, QTimer
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QAbstractItemView,
    QApplication,
    QCheckBox,
    QComboBox,
    QFileDialog,
    QFrame,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QProgressBar,
    QPushButton,
    QScrollArea,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from . import logfile
from .device import (
    DumpWorker,
    LogDevice,
    available_ports,
    guess_teensy_port,
)
from .logfile import Chunk, Session, format_bytes, format_duration
from .plot import TracePlot
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

FOLDER_SOURCE = "__folder__"  # userData marking the storage-directory source
POLL_INTERVAL_MS = 100  # how often a running dump's progress is picked up
PREVIEW_MAX_ROWS = 2000  # a session can hold six figures of records; the
# table is for eyeballing what came back, the CSV is for actually reading it


def _mono(item: QTableWidgetItem) -> QTableWidgetItem:
    item.setFont(MONO_FONT)
    return item


class StatusCard(QFrame):
    """The device readout: a row of label/value pairs. Which fields exist
    depends on the source -- a storage directory has no session or capture
    state to report, only what its files contain."""

    SERIAL_FIELDS = ["Device", "Session", "Captures", "Recorded", "Chunk"]
    FOLDER_FIELDS = ["Folder", "Chunk files", "Captures", "Size"]

    def __init__(self) -> None:
        super().__init__()
        self.setObjectName("Card")
        self._layout = QHBoxLayout(self)
        self._layout.setContentsMargins(18, 14, 18, 14)
        self._layout.setSpacing(34)
        self._values: dict[str, QLabel] = {}
        self.show_fields(self.SERIAL_FIELDS)

    def show_fields(self, fields: list[str]) -> None:
        while self._layout.count():
            item = self._layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
            elif item.layout():
                # Nested field column: drop its children with it.
                while item.layout().count():
                    child = item.layout().takeAt(0)
                    if child.widget():
                        child.widget().deleteLater()

        self._values = {}
        for name in fields:
            column = QVBoxLayout()
            column.setSpacing(2)
            label = QLabel(name.upper())
            label.setObjectName("CardLabel")
            value = QLabel("—")
            value.setObjectName("CardValue")
            column.addWidget(label)
            column.addWidget(value)
            self._layout.addLayout(column)
            self._values[name] = value
        self._layout.addStretch(1)

    def set(self, field: str, text: str, color: str | None = None) -> None:
        label = self._values.get(field)
        if label is None:
            return
        label.setText(text)
        label.setStyleSheet(f"color: {color};" if color else "")

    def clear_values(self) -> None:
        for field in self._values:
            self.set(field, "—")


class DeviceTab(QWidget):
    """Status plus the three actions. Everything that talks to the device
    lives here; the Logs page only ever sees already-decoded chunks."""

    def __init__(self, window: "MainWindow") -> None:
        super().__init__()
        self.window = window
        self.raw_dump: bytes = b""
        self._worker: DumpWorker | None = None
        self._dump_started_at = 0.0

        layout = QVBoxLayout(self)
        layout.setContentsMargins(18, 16, 18, 16)
        layout.setSpacing(12)

        self.card = StatusCard()
        layout.addWidget(self.card)

        self.banner = self._build_banner()
        self.banner.setVisible(False)
        layout.addWidget(self.banner)

        controls = QHBoxLayout()
        self.download_btn = QPushButton("Download Log")
        mark_primary(self.download_btn)
        self.download_btn.setMinimumWidth(150)
        self.download_btn.clicked.connect(self._download)
        controls.addWidget(self.download_btn)

        self.cancel_btn = QPushButton("Cancel")
        self.cancel_btn.setEnabled(False)
        self.cancel_btn.setToolTip(
            "Stop reading. Whatever arrived so far still decodes -- chunk "
            "boundaries are self-describing, so a cut-short dump is a "
            "partial log, not a corrupt one."
        )
        self.cancel_btn.clicked.connect(self._cancel)
        controls.addWidget(self.cancel_btn)

        self.refresh_btn = QPushButton("Refresh Status")
        self.refresh_btn.clicked.connect(self.refresh_status)
        controls.addWidget(self.refresh_btn)

        controls.addStretch(1)

        self.save_raw_btn = QPushButton("Save Raw Dump…")
        self.save_raw_btn.setEnabled(False)
        self.save_raw_btn.setToolTip(
            "Save the undecoded bytes exactly as they arrived -- the thing "
            "to keep if a decode ever looks wrong."
        )
        self.save_raw_btn.clicked.connect(self._save_raw)
        controls.addWidget(self.save_raw_btn)

        self.erase_btn = QPushButton("Erase Device Log…")
        self.erase_btn.setToolTip("LOGCLEAR — formats the device's log store.")
        self.erase_btn.clicked.connect(self._erase)
        controls.addWidget(self.erase_btn)
        layout.addLayout(controls)

        self.progress = QProgressBar()
        self.progress.setTextVisible(False)
        self.progress.setFixedHeight(6)
        self.progress.setVisible(False)
        layout.addWidget(self.progress)

        self.activity = QPlainTextEdit()
        self.activity.setReadOnly(True)
        self.activity.setFont(MONO_FONT)
        self.activity.setPlaceholderText(
            "Commands sent to the device, and what came back, show up here."
        )
        layout.addWidget(self.activity, stretch=1)

        self.timer = QTimer(self)
        self.timer.setInterval(POLL_INTERVAL_MS)
        self.timer.timeout.connect(self._poll_dump)

    def _build_banner(self) -> QFrame:
        banner = QFrame()
        banner.setObjectName("Banner")
        inner = QHBoxLayout(banner)
        inner.setContentsMargins(12, 8, 12, 8)
        self.banner_text = QLabel()
        self.banner_text.setObjectName("BannerText")
        inner.addWidget(self.banner_text)
        inner.addStretch(1)
        return banner

    def log(self, text: str) -> None:
        self.activity.appendPlainText(text)

    # --- source ---------------------------------------------------------

    def on_source_changed(self) -> None:
        """Retargets the actions at whatever the top bar now points to.
        Erase and the capture/session readouts only mean anything against a
        real device, so they go away for a storage directory."""
        is_folder = self.window.source_is_folder()
        self.download_btn.setText("Read Storage Folder" if is_folder else "Download Log")
        self.erase_btn.setEnabled(not is_folder)
        self.refresh_btn.setEnabled(not is_folder)
        self.card.show_fields(
            StatusCard.FOLDER_FIELDS if is_folder else StatusCard.SERIAL_FIELDS
        )
        if is_folder:
            self._show_folder_status()
        else:
            self.banner.setVisible(False)

    def _show_folder_status(self) -> None:
        directory = self.window.folder
        if directory is None:
            return
        try:
            paths = [p for p in directory.iterdir() if logfile.CHUNK_FILE_RE.match(p.name)]
            total = sum(p.stat().st_size for p in paths)
        except OSError as e:
            self.card.set("Folder", "unreadable", RED)
            self.log(f"! {e}")
            return
        captures = logfile.read_index(directory)
        self.card.set("Folder", directory.name)
        self.card.set("Chunk files", str(len(paths)))
        self.card.set("Captures", str(len(captures)) if captures else "—")
        self.card.set("Size", format_bytes(total))

    def refresh_status(self) -> None:
        if self.window.source_is_folder():
            self._show_folder_status()
            return
        port = self.window.selected_port()
        if not port:
            QMessageBox.information(self, "Status", "No serial port selected.")
            return

        try:
            status = LogDevice(port).status()
        except Exception as e:  # noqa: BLE001 -- any serial failure is reportable
            self.card.clear_values()
            self.card.set("Device", "unreachable", RED)
            self.window.set_status("Unreachable", RED)
            self.log(f"! LOGSTATUS on {port}: {e}")
            return

        if status is None:
            self.card.clear_values()
            self.card.set("Device", "no reply", AMBER)
            self.window.set_status("No reply", AMBER)
            self.log(
                f"! {port} answered nothing status-shaped. Is this the "
                f"dashboard (env:teensymm) and not the CAN sniffer?"
            )
            return

        self.card.set("Device", "ready" if status.ready else "not ready",
                      GREEN if status.ready else AMBER)
        self.card.set("Session", str(status.session_id))
        self.card.set("Captures", str(status.log_count))
        self.card.set("Recorded", format_duration(status.recorded_ms))
        self.card.set("Chunk", str(status.chunk_index))

        self.banner_text.setText(
            "Still recording. Stop the capture on the device (swipe up → "
            "toggle Logging off) before downloading, or the last few "
            "seconds are still sitting unflushed in its RAM buffer."
        )
        self.banner.setVisible(status.capturing)
        self.window.set_status(
            f"{status.log_count} captures · {format_duration(status.recorded_ms)} recorded",
            ACCENT if status.capturing else GREEN,
        )
        self.log(f"< {status.raw}")

    # --- download -------------------------------------------------------

    def _download(self) -> None:
        if self.window.source_is_folder():
            self._read_folder()
        else:
            self._start_serial_dump()

    def _read_folder(self) -> None:
        if self.window.folder is None:
            self.window.choose_folder()  # not picked yet -- ask now
        directory = self.window.folder
        if directory is None:
            return  # dialog cancelled
        try:
            data = logfile.read_storage_dir(directory)
        except OSError as e:
            QMessageBox.warning(self, "Read Folder", f"Couldn't read {directory}:\n{e}")
            return
        self.log(f"> read {directory} — {format_bytes(len(data))}")
        self._ingest(data)

    def _start_serial_dump(self) -> None:
        port = self.window.selected_port()
        if not port:
            QMessageBox.information(self, "Download", "No serial port selected.")
            return

        self._worker = DumpWorker(LogDevice(port))
        self._dump_started_at = time.monotonic()
        try:
            self._worker.start()
        except Exception as e:  # noqa: BLE001
            self._worker = None
            QMessageBox.critical(self, "Download failed", str(e))
            return

        self.log(f"> LOGDUMP on {port}")
        self._set_busy(True)
        self.timer.start()

    def _cancel(self) -> None:
        if self._worker is not None:
            self._worker.cancel()
            self.log("> cancel requested")

    def _poll_dump(self) -> None:
        if self._worker is None:
            return
        done, received, data, error = self._worker.poll()
        self.window.set_status(f"Receiving… {format_bytes(received)}", ACCENT)
        if not done:
            return

        cancelled = self._worker.cancelled
        self.timer.stop()
        self._worker = None
        self._set_busy(False)

        if error is not None:
            self.window.set_status("Download failed", RED)
            self.log(f"! {error}")
            QMessageBox.critical(self, "Download failed", error)
            return

        elapsed = time.monotonic() - self._dump_started_at
        self.log(
            f"< {format_bytes(len(data))} in {elapsed:.1f}s"
            + (" (cancelled — partial)" if cancelled else "")
        )
        self._ingest(data)

    def _ingest(self, data: bytes) -> None:
        """Decodes a dump (however it was obtained) and hands it to the Logs
        page."""
        self.raw_dump = data
        self.save_raw_btn.setEnabled(bool(data))

        chunks = logfile.parse_chunks(data)
        sessions = logfile.group_sessions(chunks)
        # Counted the same way the Logs page counts them (empty sessions
        # set aside), so the two readouts can't disagree.
        recorded = [s for s in sessions if s.record_count]
        empty = len(sessions) - len(recorded)
        records = sum(len(c.records) for c in chunks)
        self.log(
            f"= {len(chunks)} chunks · {records:,} records · "
            f"{len(recorded)} sessions" + (f" ({empty} empty)" if empty else "")
        )
        if not chunks:
            self.window.set_status("Nothing decoded", AMBER)
            QMessageBox.information(
                self,
                "Download",
                "No log chunks in what came back.\n\n"
                "Either the device has never recorded anything, or this "
                "isn't the dashboard firmware's serial port.",
            )
            return

        self.window.logs_tab.set_chunks(chunks)
        # Only the sessions that recorded something: an empty one has no
        # records to draw and would just be a dead entry in the picker.
        self.window.plot_tab.set_sessions(recorded)
        self.window.set_status(f"{records:,} records · {len(recorded)} sessions", GREEN)
        self.window.select_page(1)

    def _set_busy(self, busy: bool) -> None:
        self.download_btn.setEnabled(not busy)
        self.erase_btn.setEnabled(not busy and not self.window.source_is_folder())
        self.refresh_btn.setEnabled(not busy and not self.window.source_is_folder())
        self.cancel_btn.setEnabled(busy)
        self.window.source_combo.setEnabled(not busy)
        # Indeterminate: LOGDUMP has no length prefix, so the total is
        # genuinely unknown until the stream goes quiet (see device.py).
        self.progress.setRange(0, 0)
        self.progress.setVisible(busy)

    # --- other actions --------------------------------------------------

    def _save_raw(self) -> None:
        if not self.raw_dump:
            return
        path, _ = QFileDialog.getSaveFileName(
            self, "Save Raw Dump", str(Path.home() / "mqb_log_dump.bin"),
            "Raw dump (*.bin)",
        )
        if not path:
            return
        try:
            Path(path).write_bytes(self.raw_dump)
        except OSError as e:
            QMessageBox.warning(self, "Save Raw Dump", f"Couldn't write {path}:\n{e}")
            return
        self.log(f"= wrote {format_bytes(len(self.raw_dump))} to {path}")

    def _erase(self) -> None:
        port = self.window.selected_port()
        if not port:
            return
        confirm = QMessageBox(self)
        confirm.setWindowTitle("Erase Device Log")
        confirm.setIcon(QMessageBox.Warning)
        confirm.setText("Erase every log on the device?")
        confirm.setInformativeText(
            "LOGCLEAR formats the log store. Every capture on the device is "
            "gone, and nothing on the host is a backup unless you already "
            "downloaded it."
        )
        confirm.setStandardButtons(QMessageBox.Cancel | QMessageBox.Yes)
        confirm.setDefaultButton(QMessageBox.Cancel)
        if confirm.exec() != QMessageBox.Yes:
            return

        try:
            LogDevice(port).clear()
        except Exception as e:  # noqa: BLE001
            self.log(f"! LOGCLEAR: {e}")
            QMessageBox.critical(self, "Erase failed", str(e))
            return
        self.log("> LOGCLEAR — log store formatted")
        self.refresh_status()


class LogsTab(QWidget):
    """What came back, grouped by session. Selecting sessions scopes both
    the preview below and what Export CSV writes."""

    COLUMNS = ["Session", "Chunks", "Records", "Duration", "Signals"]
    PREVIEW_COLUMNS = ["Time", "Signal", "Value"]

    def __init__(self, window: "MainWindow") -> None:
        super().__init__()
        self.window = window
        self.all_sessions: list[Session] = []  # everything the dump held
        self.sessions: list[Session] = []  # what the table is showing, which
        # is what row indices and Export CSV are relative to
        self.names = logfile.load_signal_names()
        self._chunk_count = 0

        layout = QVBoxLayout(self)
        layout.setContentsMargins(18, 16, 18, 16)
        layout.setSpacing(10)

        controls = QHBoxLayout()
        self.summary = QLabel("Nothing downloaded yet.")
        self.summary.setObjectName("StatsLabel")
        controls.addWidget(self.summary)
        self.show_empty = QCheckBox()
        self.show_empty.setVisible(False)  # nothing to offer until a dump
        # actually contains some
        self.show_empty.setToolTip(
            "A boot that created a chunk and then recorded nothing — "
            "logging left off. Hidden by default so they don't interleave "
            "with the sessions you're picking between."
        )
        self.show_empty.toggled.connect(self._rebuild)
        controls.addWidget(self.show_empty)
        controls.addStretch(1)

        self.plot_btn = QPushButton("Plot Session")
        self.plot_btn.setEnabled(False)
        self.plot_btn.setToolTip(
            "Draw this session's signals against time on the Plot page. "
            "Needs exactly one session selected — boot-relative timestamps "
            "give two sessions no common time axis."
        )
        self.plot_btn.clicked.connect(self._plot)
        controls.addWidget(self.plot_btn)

        self.export_btn = QPushButton("Export CSV…")
        mark_primary(self.export_btn)
        self.export_btn.setEnabled(False)
        self.export_btn.setToolTip(
            "Write the selected sessions to CSV — every session if none are "
            "selected. Records come out in true chronological order."
        )
        self.export_btn.clicked.connect(self._export)
        controls.addWidget(self.export_btn)
        layout.addLayout(controls)

        self.table = QTableWidget(0, len(self.COLUMNS))
        self.table.setHorizontalHeaderLabels(self.COLUMNS)
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.horizontalHeader().setSectionResizeMode(4, QHeaderView.Stretch)
        self.table.itemSelectionChanged.connect(self._on_selection)
        layout.addWidget(self.table, stretch=1)

        self.preview_label = QLabel("Select a session to preview its records.")
        self.preview_label.setObjectName("StatsLabel")
        layout.addWidget(self.preview_label)

        self.preview = QTableWidget(0, len(self.PREVIEW_COLUMNS))
        self.preview.setHorizontalHeaderLabels(self.PREVIEW_COLUMNS)
        self.preview.verticalHeader().setVisible(False)
        self.preview.setEditTriggers(QTableWidget.NoEditTriggers)
        self.preview.horizontalHeader().setSectionResizeMode(1, QHeaderView.Stretch)
        layout.addWidget(self.preview, stretch=2)

    def set_chunks(self, chunks: list[Chunk]) -> None:
        self.names = logfile.load_signal_names()  # picked up per download, so
        # a signal renamed in the registry shows up without a restart
        self.all_sessions = logfile.group_sessions(chunks)
        self._chunk_count = len(chunks)

        empty = sum(1 for s in self.all_sessions if not s.record_count)
        self.show_empty.setText(
            f"Show {empty} empty session{'s' if empty != 1 else ''}"
        )
        self.show_empty.setVisible(bool(empty))
        self._rebuild()

    def _rebuild(self) -> None:
        """Fills the table from all_sessions, honoring the empty-session
        toggle. Separate from set_chunks so flipping the toggle doesn't
        re-decode anything."""
        keep_selected = {s.session_id for s in self._selected_sessions()}
        self.sessions = [
            s for s in self.all_sessions
            if s.record_count or self.show_empty.isChecked()
        ]
        self.export_btn.setEnabled(bool(self.sessions))

        records = sum(s.record_count for s in self.sessions)
        self.summary.setText(
            f"{len(self.sessions)} sessions · {self._chunk_count} chunks · "
            f"{records:,} records"
        )

        self.table.setRowCount(0)
        for session in self.sessions:
            row = self.table.rowCount()
            self.table.insertRow(row)
            signal_names = sorted(
                self.names.get(sid, f"#{sid}") for sid in session.signal_ids
            )
            cells = [
                str(session.session_id),
                str(len(session.chunks)),
                f"{session.record_count:,}",
                format_duration(session.duration_ms),
                ", ".join(signal_names) or "—",
            ]
            for col, text in enumerate(cells):
                item = QTableWidgetItem(text)
                if col < 4:
                    _mono(item)
                    item.setTextAlignment(Qt.AlignRight | Qt.AlignVCenter)
                self.table.setItem(row, col, item)
        self.table.resizeColumnsToContents()
        self.table.horizontalHeader().setSectionResizeMode(4, QHeaderView.Stretch)

        # Flipping the toggle shouldn't cost you your selection; only a
        # fresh download falls back to the newest session, which is the one
        # you almost always just recorded.
        rows = [i for i, s in enumerate(self.sessions) if s.session_id in keep_selected]
        if rows:
            self.table.clearSelection()
            for row in rows:
                self.table.selectRow(row)
        elif self.sessions:
            self.table.selectRow(0)

    def _selected_sessions(self) -> list[Session]:
        rows = {i.row() for i in self.table.selectedIndexes()}
        return [self.sessions[r] for r in sorted(rows) if r < len(self.sessions)]

    def _on_selection(self) -> None:
        selected = self._selected_sessions()
        self.plot_btn.setEnabled(len(selected) == 1)
        self.preview.setRowCount(0)
        if len(selected) != 1:
            self.preview_label.setText(
                "Select one session to preview its records."
                if selected else "Select a session to preview its records."
            )
            return

        session = selected[0]
        start_ms, _ = session.timestamp_span_ms
        rows = [
            (r[0], r[1], r[2])
            for chunk in logfile.sort_chunks(session.chunks)
            for r in chunk.records
        ]
        shown = rows[:PREVIEW_MAX_ROWS]
        self.preview.setRowCount(len(shown))
        for row, (timestamp_ms, signal_id, value) in enumerate(shown):
            cells = [
                f"{(timestamp_ms - start_ms) / 1000:.2f}",
                self.names.get(signal_id, f"#{signal_id}"),
                f"{value:.3f}",
            ]
            for col, text in enumerate(cells):
                item = QTableWidgetItem(text)
                if col != 1:
                    _mono(item)
                    item.setTextAlignment(Qt.AlignRight | Qt.AlignVCenter)
                self.preview.setItem(row, col, item)
        self.preview.resizeColumnsToContents()
        self.preview.horizontalHeader().setSectionResizeMode(1, QHeaderView.Stretch)

        # Boot-relative timestamps are all the hardware has (no RTC), which
        # is why the preview counts seconds from the session's own start.
        if len(rows) > len(shown):
            self.preview_label.setText(
                f"Session {session.session_id} — first {len(shown):,} of "
                f"{len(rows):,} records. Export the CSV for all of them."
            )
        else:
            self.preview_label.setText(
                f"Session {session.session_id} — {len(rows):,} records, "
                f"seconds from session start."
            )

    def _plot(self) -> None:
        selected = self._selected_sessions()
        if len(selected) == 1:
            self.window.plot_tab.show_session(selected[0].session_id)

    def _export(self) -> None:
        selected = self._selected_sessions() or self.sessions
        if not selected:
            return
        chunks = [c for s in selected for c in s.chunks]

        default = "mqb_log.csv" if len(selected) != 1 else f"mqb_log_session{selected[0].session_id}.csv"
        path, _ = QFileDialog.getSaveFileName(
            self, "Export CSV", str(Path.home() / default), "CSV (*.csv)"
        )
        if not path:
            return
        try:
            total = logfile.write_csv(chunks, Path(path), self.names)
        except OSError as e:
            QMessageBox.warning(self, "Export CSV", f"Couldn't write {path}:\n{e}")
            return
        self.window.device_tab.log(f"= exported {total} records to {path}")
        QMessageBox.information(
            self, "Export CSV",
            f"Wrote {total:,} records from {len(selected)} "
            f"session{'s' if len(selected) != 1 else ''} to\n{path}",
        )


class PlotTab(QWidget):
    """One session's signals against time.

    Deliberately a separate page from Logs rather than a mode of it: the
    Logs page picks *what to keep* (select sessions, export CSV) and can
    select several at once, while a plot only means anything within a single
    session -- timestamps are boot-relative, so two sessions share no time
    axis to draw them on. The two paths are independent; exporting never
    needs a plot to have been drawn, and vice versa.

    Building the traces walks every record in the session, so it happens
    when this page is actually shown (or the session changed while it is),
    not on every download."""

    LIST_WIDTH = 190

    def __init__(self, window: "MainWindow") -> None:
        super().__init__()
        self.window = window
        self.sessions: list[Session] = []
        self.specs = logfile.load_signal_specs()
        self._shown_session: int | None = None  # what the plot currently holds
        self._pending: int | None = None  # what it should hold, once visible

        layout = QVBoxLayout(self)
        layout.setContentsMargins(18, 16, 18, 16)
        layout.setSpacing(10)

        controls = QHBoxLayout()
        label = QLabel("Session")
        label.setObjectName("FieldLabel")
        controls.addWidget(label)

        self.session_combo = QComboBox()
        self.session_combo.setMinimumWidth(240)
        self.session_combo.setToolTip(
            "One power cycle of the device. Boot-relative timestamps don't "
            "line up between sessions, so only one is plotted at a time."
        )
        self.session_combo.setEnabled(False)  # until a dump brings some in
        self.session_combo.currentIndexChanged.connect(self._on_session_changed)
        controls.addWidget(self.session_combo)

        self.summary = QLabel("Nothing downloaded yet.")
        self.summary.setObjectName("StatsLabel")
        controls.addWidget(self.summary)
        controls.addStretch(1)

        hint = QLabel("scroll to zoom · drag to pan · double-click to reset")
        hint.setObjectName("StatsLabel")
        controls.addWidget(hint)
        layout.addLayout(controls)

        body = QHBoxLayout()
        body.setSpacing(10)

        self.signal_list = QListWidget()
        self.signal_list.setFixedWidth(self.LIST_WIDTH)
        self.signal_list.setToolTip(
            "Which signals are drawn. Rows are grouped by unit, so hiding "
            "the last signal of a kind drops its row and gives the height "
            "to the others."
        )
        self.signal_list.itemChanged.connect(self._on_signal_toggled)
        body.addWidget(self.signal_list)

        self.plot = TracePlot()
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setWidget(self.plot)
        body.addWidget(scroll, stretch=1)
        layout.addLayout(body, stretch=1)

    # --- data -----------------------------------------------------------

    def set_sessions(self, sessions: list[Session]) -> None:
        """Called with each fresh download. Keeps the session you were
        looking at selected if the new dump still has it -- re-downloading
        after recording more shouldn't move you somewhere else."""
        self.specs = logfile.load_signal_specs()  # same reason LogsTab
        # reloads names: a signal renamed in the registry shows up without
        # a restart
        self.sessions = sessions
        previous = self._current_session_id()

        self.session_combo.blockSignals(True)
        self.session_combo.clear()
        for session in sessions:
            self.session_combo.addItem(
                f"Session {session.session_id} — "
                f"{format_duration(session.duration_ms)}, "
                f"{session.record_count:,} records",
                userData=session.session_id,
            )
        index = self.session_combo.findData(previous) if previous is not None else -1
        self.session_combo.setCurrentIndex(index if index >= 0 else 0)
        self.session_combo.blockSignals(False)
        self.session_combo.setEnabled(bool(sessions))

        self._shown_session = None  # the records behind it may have changed
        self._pending = self._current_session_id()
        self._build_if_visible()

    def show_session(self, session_id: int) -> None:
        """Entry point for the Logs page's Plot button."""
        index = self.session_combo.findData(session_id)
        if index >= 0:
            self.session_combo.setCurrentIndex(index)  # drives _on_session_changed
        self.window.select_page(2)

    def _current_session_id(self) -> int | None:
        return self.session_combo.currentData() if self.session_combo.count() else None

    def _session(self, session_id: int | None) -> Session | None:
        return next((s for s in self.sessions if s.session_id == session_id), None)

    def _on_session_changed(self) -> None:
        self._pending = self._current_session_id()
        self._build_if_visible()

    def showEvent(self, event) -> None:
        super().showEvent(event)
        self._build_if_visible()

    def _build_if_visible(self) -> None:
        if self.isVisible() and self._pending != self._shown_session:
            self._build(self._pending)

    def _build(self, session_id: int | None) -> None:
        session = self._session(session_id)
        if session is None:
            self.plot.set_traces([])
            self.signal_list.clear()
            self.summary.setText("Nothing downloaded yet.")
            self._shown_session = None
            return

        # A long session is six figures of records; splitting them into
        # traces and summarizing each one is the only O(records) work the
        # page does, and it is visible on a big dump.
        QApplication.setOverrideCursor(Qt.WaitCursor)
        try:
            traces = logfile.build_traces(session, self.specs)
            self.plot.set_traces(traces)
        finally:
            QApplication.restoreOverrideCursor()
        self._shown_session = session_id

        self.signal_list.blockSignals(True)
        self.signal_list.clear()
        for trace in traces:
            item = QListWidgetItem(trace.name)
            item.setData(Qt.UserRole, trace.signal_id)
            item.setCheckState(Qt.Checked)
            item.setForeground(QColor(self.plot.color_for(trace.signal_id)))
            item.setToolTip(f"{len(trace):,} records")
            self.signal_list.addItem(item)
        self.signal_list.blockSignals(False)

        units = len({t.unit for t in traces})
        self.summary.setText(
            f"{session.record_count:,} records · {len(traces)} signals · "
            f"{units} axis group{'s' if units != 1 else ''}"
        )

    def _on_signal_toggled(self) -> None:
        hidden = {
            self.signal_list.item(i).data(Qt.UserRole)
            for i in range(self.signal_list.count())
            if self.signal_list.item(i).checkState() != Qt.Checked
        }
        self.plot.set_hidden(hidden)


class MainWindow(QMainWindow):
    PAGES = ["Device", "Logs", "Plot"]

    def __init__(self, port: str | None = None, folder: Path | None = None) -> None:
        super().__init__()
        self.setWindowTitle("MQB Log Export — black-box log offload")
        self.resize(1000, 720)

        self.settings = QSettings("MQBMonitor", "logexport")
        self.folder: Path | None = folder
        self._preselect_port = port

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
        self.device_tab = DeviceTab(self)
        self.logs_tab = LogsTab(self)
        self.plot_tab = PlotTab(self)
        self.stack.addWidget(self.device_tab)
        self.stack.addWidget(self.logs_tab)
        self.stack.addWidget(self.plot_tab)
        body_layout.addWidget(self.stack, stretch=1)

        root_layout.addWidget(body, stretch=1)
        self.setCentralWidget(root)

        self.sidebar.currentRowChanged.connect(self.stack.setCurrentIndex)
        self.sidebar.setCurrentRow(0)

        # Deferred until the tabs exist, since it drives them.
        self.source_combo.currentIndexChanged.connect(self._on_source_changed)
        if folder is not None:
            self._use_folder(folder)  # from the command line: no dialog
        # Only the tab update, not _on_source_changed: with no serial ports
        # attached the folder item is the only one there is, and prompting
        # for a directory before the window has even appeared is startling.
        self.device_tab.on_source_changed()

    # --- shell ----------------------------------------------------------

    def _build_sidebar(self) -> QListWidget:
        sidebar = QListWidget()
        sidebar.setObjectName("Sidebar")
        sidebar.setFixedWidth(150)
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
        title = QLabel("MQB Log Export")
        title.setObjectName("AppTitle")
        subtitle = QLabel("black-box log offload")
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
        self.source_combo.setMinimumWidth(260)
        self.source_combo.setToolTip(
            "The dashboard's own USB serial port (env:teensymm — not the "
            "CAN sniffer), or a storage directory: the simulator's "
            "sim_storage/, or a copy of one."
        )
        self._populate_ports()
        layout.addWidget(self.source_combo)

        refresh = QPushButton("Refresh")
        refresh.setToolTip("Re-scan serial ports")
        refresh.clicked.connect(self._populate_ports)
        layout.addWidget(refresh)

        layout.addStretch(1)

        self.status_dot = QLabel("●")
        self.status_dot.setObjectName("StatusDot")
        self.status_dot.setStyleSheet(f"color: {TEXT_DIM};")
        layout.addWidget(self.status_dot)
        self.status = QLabel("Idle")
        self.status.setObjectName("StatsLabel")
        self.status.setFont(MONO_FONT)
        # Fixed + right-aligned so a growing byte count during a dump doesn't
        # keep reflowing the trailing group of the bar.
        self.status.setMinimumWidth(300)
        self.status.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        layout.addWidget(self.status)
        return bar

    def set_status(self, text: str, color: str = TEXT_DIM) -> None:
        self.status.setText(text)
        self.status_dot.setStyleSheet(f"color: {color};")

    def select_page(self, index: int) -> None:
        self.sidebar.setCurrentRow(index)  # drives self.stack via the signal

    # --- source ---------------------------------------------------------

    def _populate_ports(self) -> None:
        current = self.source_combo.currentData() if self.source_combo.count() else None
        self.source_combo.blockSignals(True)
        self.source_combo.clear()
        for dev, desc in available_ports():
            self.source_combo.addItem(f"{dev} — {desc}", userData=dev)
        self.source_combo.addItem("Storage folder…", userData=FOLDER_SOURCE)

        target = self._preselect_port or current or guess_teensy_port()
        index = self.source_combo.findData(target) if target else -1
        self.source_combo.setCurrentIndex(index if index >= 0 else 0)
        self.source_combo.blockSignals(False)

    def _on_source_changed(self) -> None:
        # Picking the folder item always re-prompts -- it's how you switch
        # to a different storage directory, and it's what the ellipsis in
        # its label promises. Only user interaction reaches here;
        # _populate_ports and _use_folder both set the index with signals
        # blocked.
        if self.source_combo.currentData() == FOLDER_SOURCE:
            self.choose_folder()
        self.device_tab.on_source_changed()
        self.set_status("Idle")

    def choose_folder(self) -> None:
        """Picks the storage directory the folder source reads, defaulting
        to the last one used (or the repo's own sim_storage/, which is where
        the simulator writes)."""
        remembered = self.settings.value("folder", "")
        fallback = logfile.REPO_ROOT / "sim_storage"
        start = remembered or (str(fallback) if fallback.is_dir() else str(Path.home()))
        chosen = QFileDialog.getExistingDirectory(self, "Choose storage folder", start)
        if not chosen:
            if self.folder is None and self.source_combo.count() > 1:
                self.source_combo.setCurrentIndex(0)  # cancelled: fall back
            return

        self._use_folder(Path(chosen))

    def _use_folder(self, folder: Path) -> None:
        """Points the folder source at `folder` and selects it, without
        going through the dialog (the --folder command-line path)."""
        self.folder = folder
        self.settings.setValue("folder", str(folder))
        index = self.source_combo.findData(FOLDER_SOURCE)
        self.source_combo.blockSignals(True)
        self.source_combo.setItemText(index, f"Folder — {folder.name}")
        self.source_combo.setCurrentIndex(index)
        self.source_combo.blockSignals(False)

    def source_is_folder(self) -> bool:
        return self.source_combo.currentData() == FOLDER_SOURCE

    def selected_port(self) -> str | None:
        data = self.source_combo.currentData()
        return None if data == FOLDER_SOURCE else data


def run_gui(port: str | None = None, folder: Path | None = None) -> int:
    app = QApplication.instance() or QApplication([])
    apply_theme(app)
    win = MainWindow(port=port, folder=folder)
    win.show()
    return app.exec()
