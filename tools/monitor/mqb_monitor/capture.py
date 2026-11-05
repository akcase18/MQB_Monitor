"""Capture files: every raw CAN frame of a session, written as it arrives
(the top bar's Record toggle) and read back (FileSource replay, and the
fake car's recorded answers).

A capture is a CSV after one '#' header line:

  # mqb-monitor capture v1; source=serial:/dev/cu.usbmodem1; started=2026-10-29T18:04:11
  time_s,epoch_s,direction,can_id,dlc,data_hex,note
  0.000,1793297051.123,rx,7E0,8,03 22 F4 05 00 00 00 00,

`time_s` counts from the first recorded frame; `epoch_s` is wall-clock time,
for lining a capture up against a phone screen recording. The other columns
match Traffic's raw Save Log, and read_capture() reads those files too (they
have no header line or epoch).

No PySide6.
"""
from __future__ import annotations

import csv
import io
import threading
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

from .can_frame import CanFrame

CAPTURE_DIR = Path(__file__).resolve().parents[1] / "captures"
COLUMNS = ["time_s", "epoch_s", "direction", "can_id", "dlc", "data_hex", "note"]
HEADER_PREFIX = "# mqb-monitor capture v1"
SYNTHETIC_SOURCES = ("demo", "simulated")
FLUSH_EVERY_S = 0.5


@dataclass
class CaptureInfo:
    source: str = ""  # "demo", "serial:<port>", ... ("" for a Save Log file)
    started: str = ""  # ISO local time recording started

    @property
    def is_demo(self) -> bool:
        """Made-up traffic: the demo source or the simulated dongle."""
        return self.source in SYNTHETIC_SOURCES


def new_capture_path(source: str, now: datetime | None = None) -> Path:
    """captures/<YYYY-MM-DD_HHMMSS>.csv, with _demo on demo-traffic captures
    so they can't be mistaken for the car's."""
    stamp = (now or datetime.now()).strftime("%Y-%m-%d_%H%M%S")
    suffix = "_demo" if source in SYNTHETIC_SOURCES else ""
    return CAPTURE_DIR / f"{stamp}{suffix}.csv"


class CaptureWriter:
    """Appends frames to a capture file as they arrive. write() is called
    from the source thread, close() from the GUI's, hence the lock."""

    def __init__(self, path: Path, source: str) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        self.path = path
        self.count = 0
        self._lock = threading.Lock()
        self._file = open(path, "w", newline="")
        self._csv = csv.writer(self._file)
        started = datetime.now().isoformat(timespec="seconds")
        self._file.write(f"{HEADER_PREFIX}; source={source}; started={started}\n")
        self._csv.writerow(COLUMNS)
        self._first: float | None = None
        # CanFrame timestamps are monotonic; this turns one into wall time.
        self._epoch_offset = time.time() - time.monotonic()
        self._last_flush = time.monotonic()

    def write(self, frame: CanFrame, note: str = "") -> None:
        with self._lock:
            if self._file.closed:
                return
            if self._first is None:
                self._first = frame.timestamp
            self._csv.writerow([
                f"{frame.timestamp - self._first:.4f}",
                f"{frame.timestamp + self._epoch_offset:.4f}",
                frame.direction,
                frame.id_str(),
                len(frame.data),
                frame.hex_data(),
                note,
            ])
            self.count += 1
            now = time.monotonic()
            if now - self._last_flush >= FLUSH_EVERY_S:
                self._file.flush()
                self._last_flush = now

    def close(self) -> None:
        with self._lock:
            if not self._file.closed:
                self._file.close()


def _parse_header(line: str) -> CaptureInfo:
    info = CaptureInfo()
    for part in line.split(";")[1:]:
        key, _, value = part.strip().partition("=")
        if key == "source":
            info.source = value
        elif key == "started":
            info.started = value
    return info


def read_capture(path: Path) -> tuple[CaptureInfo, list[tuple[float, CanFrame]]]:
    """Every frame in a capture (or a raw Save Log), as (time_s, frame)
    pairs in file order. The frames' own timestamps are time_s too; a
    replay rebases them. Raises ValueError on a file that isn't one."""
    text = Path(path).read_text()
    info = CaptureInfo()
    lines = text.splitlines()
    while lines and lines[0].startswith("#"):
        if lines[0].startswith(HEADER_PREFIX):
            info = _parse_header(lines[0])
        lines.pop(0)
    reader = csv.DictReader(io.StringIO("\n".join(lines)))
    if not reader.fieldnames or not {"time_s", "can_id", "data_hex"} <= set(reader.fieldnames):
        raise ValueError(f"{Path(path).name} isn't a capture or raw traffic log")
    frames = []
    for n, row in enumerate(reader, start=2):
        try:
            t = float(row["time_s"])
            can_id = row["can_id"].strip()
            frame = CanFrame(
                t, int(can_id, 16), bytes.fromhex(row["data_hex"]),
                extended=len(can_id) > 3,
                direction=row.get("direction") or "rx",
            )
        except (ValueError, TypeError) as e:
            raise ValueError(f"{Path(path).name} line {n}: {e}") from e
        frames.append((t, frame))
    return info, frames
