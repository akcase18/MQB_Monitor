"""USB serial links to the two dashes.

BenchLink drives the v4 dash's bench mode (src/main.cpp, "Bench mode"): the
BENCHREQ handshake, configuration commands, and the frame stream it sends
unasked. One reader thread owns the port: `F` lines go to the capture file
while one is open, everything else is a reply to the last command.

DashLink sends the v5 dash's throughput stats commands (POLLSTATS,
LOOPSTATS, STATSRESET, POLLFLOOD) and parses their key=value replies.
"""

from __future__ import annotations

import queue
import threading
import time
from pathlib import Path
from typing import TextIO

import serial

BAUD = 115200  # ignored by both boards' USB serial
CONFIRM_TIMEOUT_S = 15.0
REPLY_TIMEOUT_S = 2.0


class LinkError(RuntimeError):
    pass


def parse_kv(line: str) -> dict[str, str]:
    """`WORD a=1 b=2` -> {"a": "1", "b": "2"}."""
    out: dict[str, str] = {}
    for token in line.split()[1:]:
        key, sep, value = token.partition("=")
        if sep:
            out[key] = value
    return out


class BenchLink:
    def __init__(self, port: str) -> None:
        self.port = port
        self._ser = serial.Serial(port, BAUD, timeout=0.05)
        self._ser.reset_input_buffer()
        self._replies: queue.Queue[str] = queue.Queue()
        self._capture: TextIO | None = None
        self._capture_lock = threading.Lock()
        self.frames_captured = 0
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._read, daemon=True)
        self._thread.start()

    # -- port ---------------------------------------------------------------

    def _read(self) -> None:
        buf = bytearray()
        while not self._stop.is_set():
            try:
                data = self._ser.read(4096)
            except (OSError, serial.SerialException):
                self._replies.put("LOST")
                return
            if not data:
                continue
            buf.extend(data)
            while True:
                cut = min((i for i in (buf.find(b"\r"), buf.find(b"\n")) if i >= 0), default=-1)
                if cut < 0:
                    break
                line = buf[:cut].decode("ascii", "replace").strip("\a ")
                del buf[: cut + 1]
                if not line:
                    continue
                if line.startswith("F "):
                    self._frame(line)
                else:
                    self._replies.put(line)

    def _frame(self, line: str) -> None:
        parts = line.split()
        if len(parts) < 4:
            return
        data = parts[4] if len(parts) > 4 else ""
        with self._capture_lock:
            if self._capture is not None:
                self._capture.write(f"{int(parts[1], 16)},{parts[2]},{parts[3]},{data}\n")
                self.frames_captured += 1

    def _write(self, line: str) -> None:
        self._ser.write((line + "\r").encode("ascii"))
        self._ser.flush()

    def _reply(self, timeout_s: float = REPLY_TIMEOUT_S) -> str:
        try:
            return self._replies.get(timeout=timeout_s)
        except queue.Empty as exc:
            raise LinkError(f"{self.port}: no reply") from exc

    def close(self) -> None:
        self.stop_capture()
        self._stop.set()
        self._thread.join(timeout=1.0)
        self._ser.close()

    # -- bench mode ---------------------------------------------------------

    def enter(self) -> None:
        """BENCHREQ; returns once the v4 dash is in bench mode."""
        while not self._replies.empty():
            self._replies.get_nowait()
        self._write("BENCHREQ")
        deadline = time.monotonic() + CONFIRM_TIMEOUT_S
        while time.monotonic() < deadline:
            try:
                reply = self._replies.get(timeout=0.2)
            except queue.Empty:
                continue
            if reply == "BENCHWAIT":
                print("Tap Start on the v4 dash's screen...")
            elif reply == "BENCHOK":
                return
            elif reply in ("BENCHNO", "BENCHBUSY", "SNIFFBUSY", "LOST"):
                raise LinkError(
                    {
                        "BENCHNO": "declined (or timed out) on the v4 dash",
                        "BENCHBUSY": "v4 dash isn't on the gauges",
                        "SNIFFBUSY": "v4 dash is in sniffer mode",
                        "LOST": "v4 dash disconnected",
                    }.get(reply, reply)
                )
        raise LinkError("no answer to BENCHREQ -- is this the v4 dash?")

    def leave(self) -> None:
        self._write("BENCHEND")

    def command(self, line: str) -> str:
        """Sends one bench command; raises unless it answers OK (or, for BQ,
        its BQ line)."""
        self._write(line)
        reply = self._reply()
        while reply.startswith("BENCH") or reply.startswith("TWAI"):
            reply = self._reply()  # stray status from the handshake
        if reply == "ERR" or reply == "LOST":
            raise LinkError(f"v4 refused: {line}")
        return reply

    def counters(self) -> dict[str, str]:
        return parse_kv(self.command("BQ"))

    # -- capture ------------------------------------------------------------

    def start_capture(self, path: Path) -> None:
        with self._capture_lock:
            self._capture = path.open("w")
            self._capture.write("t_us,dir,id,data\n")
            self.frames_captured = 0

    def stop_capture(self) -> int:
        with self._capture_lock:
            if self._capture is not None:
                self._capture.close()
                self._capture = None
            return self.frames_captured


class DashLink:
    """The v5 dash's stats commands. Its console also prints its own log
    lines (TWAI, Wi-Fi...), which are skipped."""

    def __init__(self, port: str) -> None:
        self.port = port
        self._ser = serial.Serial(port, BAUD, timeout=0.1)
        time.sleep(0.2)
        self._ser.reset_input_buffer()

    def close(self) -> None:
        self._ser.close()

    def _lines(self, until: str, timeout_s: float = REPLY_TIMEOUT_S) -> list[str]:
        """Lines up to and including the first starting with `until`."""
        deadline = time.monotonic() + timeout_s
        buf = bytearray()
        lines: list[str] = []
        while time.monotonic() < deadline:
            buf.extend(self._ser.read(4096))
            while b"\n" in buf:
                raw, _, rest = buf.partition(b"\n")
                buf = bytearray(rest)
                line = raw.decode("ascii", "replace").strip()
                if line:
                    lines.append(line)
                    if line.startswith(until):
                        return lines
        raise LinkError(f"{self.port}: no {until} -- is this the v5 dash, running stats firmware?")

    def _send(self, line: str) -> None:
        self._ser.reset_input_buffer()
        self._ser.write((line + "\r\n").encode("ascii"))
        self._ser.flush()

    def reset(self) -> None:
        self._send("STATSRESET")
        self._lines("OK")

    def set_flood(self, on: bool) -> None:
        self._send(f"POLLFLOOD {1 if on else 0}")
        self._lines("OK")

    def poll_stats(self) -> tuple[dict[str, str], list[dict[str, str]], list[str]]:
        """(summary, one dict per request-table entry, raw lines)."""
        self._send("POLLSTATS")
        lines = self._lines("END")
        start = next(i for i, l in enumerate(lines) if l.startswith("POLLSTATS"))
        lines = lines[start:]
        summary = parse_kv(lines[0])
        entries = [parse_kv(l) for l in lines[1:] if l.startswith("E ")]
        return summary, entries, lines

    def loop_stats(self) -> tuple[dict[str, str], str]:
        self._send("LOOPSTATS")
        line = self._lines("LOOPSTATS")[-1]
        return parse_kv(line), line
