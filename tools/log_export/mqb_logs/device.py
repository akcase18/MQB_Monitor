"""The device side: the bespoke USB-CDC line command set env:teensymm's
handle_log_command() implements (src/main.cpp), plus a background worker so
a multi-megabyte dump doesn't freeze the GUI.

Deliberately Qt-agnostic, the same way tools/monitor's sources.py is: the
worker exposes thread-safe state that the GUI polls on its existing timer,
so this module stays usable headless (dump_log.py drives it directly).

The protocol has no framing of its own -- LOGDUMP just starts writing chunk
bytes and stops when it runs out, with no length prefix and no end marker
(the firmware doesn't know the total up front; see logfile.py). End of
stream is therefore inferred from silence: IDLE_TIMEOUT_S with no bytes
arriving. That is also why a dump can't report a percentage -- only bytes
received so far.
"""
from __future__ import annotations

import threading
import time
from dataclasses import dataclass
from typing import Callable

import serial
from serial.tools import list_ports

DEFAULT_BAUD = 115200  # irrelevant for Teensy USB-CDC, kept for pyserial's API
IDLE_TIMEOUT_S = 2.0  # this much silence after the last byte = end of stream
STATUS_TIMEOUT_S = 2.0  # give up waiting for LOGSTATUS's one-line reply
SETTLE_S = 0.3  # let USB-CDC settle before the first command after opening
READ_CHUNK = 4096


def available_ports() -> list[tuple[str, str]]:
    """(device, human-readable description) for each serial port."""
    return [(p.device, p.description or p.device) for p in list_ports.comports()]


def guess_teensy_port() -> str | None:
    for p in list_ports.comports():
        text = f"{p.description} {p.manufacturer} {p.product}".lower()
        if "teensy" in text or "usb serial" in text or "usbmodem" in text:
            return p.device
    ports = list_ports.comports()
    return ports[0].device if ports else None


@dataclass
class DeviceStatus:
    """LOGSTATUS, parsed. Mirrors what handle_log_command() prints."""

    ready: bool = False
    session_id: int = 0
    chunk_index: int = 0
    capturing: bool = False
    log_count: int = 0
    recorded_ms: int = 0
    raw: str = ""


def parse_status(text: str) -> DeviceStatus | None:
    """Parses the `key=value key=value ...` status line. @returns None if
    nothing status-shaped came back (device busy, wrong port, old firmware
    without the command)."""
    fields: dict[str, str] = {}
    for token in text.split():
        key, sep, value = token.partition("=")
        if sep:
            fields[key] = value
    if "ready" not in fields:
        return None

    def as_int(key: str) -> int:
        try:
            return int(fields.get(key, "0"))
        except ValueError:
            return 0

    return DeviceStatus(
        ready=as_int("ready") == 1,
        session_id=as_int("session"),
        chunk_index=as_int("chunk"),
        capturing=as_int("capturing") == 1,
        log_count=as_int("logs"),
        recorded_ms=as_int("recorded_ms"),
        raw=text.strip(),
    )


def send_command(ser: serial.Serial, command: str) -> None:
    ser.write((command + "\r\n").encode("ascii"))
    ser.flush()


def read_line(ser: serial.Serial, timeout_s: float = STATUS_TIMEOUT_S) -> str:
    """Reads a single newline-terminated reply. LOGSTATUS is the only
    command that answers in text, and it answers in exactly one line -- so
    this returns as soon as that line lands instead of sitting out
    read_until_idle's full silence window, which is what keeps the GUI's
    status refresh feel instant."""
    deadline = time.monotonic() + timeout_s
    buf = bytearray()
    while time.monotonic() < deadline:
        data = ser.read(READ_CHUNK)
        if not data:
            continue
        buf += data
        if b"\n" in buf:
            break
    return buf.decode("ascii", "replace").strip()


def read_until_idle(
    ser: serial.Serial,
    idle_timeout_s: float = IDLE_TIMEOUT_S,
    on_progress: Callable[[int], None] | None = None,
    should_stop: Callable[[], bool] | None = None,
) -> bytes:
    """Reads until `idle_timeout_s` passes with no new bytes. `on_progress`
    is called with the running byte total; `should_stop` lets a caller
    abandon the read early (the GUI's Cancel)."""
    chunks: list[bytes] = []
    total = 0
    last_data = time.monotonic()
    while time.monotonic() - last_data < idle_timeout_s:
        if should_stop is not None and should_stop():
            break
        data = ser.read(READ_CHUNK)
        if data:
            chunks.append(data)
            total += len(data)
            last_data = time.monotonic()
            if on_progress is not None:
                on_progress(total)
    return b"".join(chunks)


class LogDevice:
    """One-shot commands against the dashboard firmware over USB serial.
    Each call opens and closes the port -- there is no session to hold, and
    leaving it open would keep the firmware's blocking command handler
    reachable while nothing is using it."""

    def __init__(self, port: str, baud: int = DEFAULT_BAUD) -> None:
        self.port = port
        self.baud = baud

    def _open(self) -> serial.Serial:
        ser = serial.Serial(self.port, self.baud, timeout=0.2)
        time.sleep(SETTLE_S)
        ser.reset_input_buffer()
        return ser

    def status(self) -> DeviceStatus | None:
        with self._open() as ser:
            send_command(ser, "LOGSTATUS")
            text = read_line(ser)
        return parse_status(text)

    def clear(self) -> None:
        """LOGCLEAR -- formats the log store. Destructive: download first."""
        with self._open() as ser:
            send_command(ser, "LOGCLEAR")
            # The firmware prints nothing back, but format() + the Logger's
            # begin() re-scan are blocking work on the device; give them a
            # moment before dropping the port so the command isn't cut off
            # mid-write by the close.
            read_until_idle(ser, 1.0)

    def dump(
        self,
        on_progress: Callable[[int], None] | None = None,
        should_stop: Callable[[], bool] | None = None,
    ) -> bytes:
        with self._open() as ser:
            send_command(ser, "LOGDUMP")
            return read_until_idle(
                ser, IDLE_TIMEOUT_S, on_progress=on_progress, should_stop=should_stop
            )


class DumpWorker:
    """Runs LogDevice.dump() on a background thread, exposing progress the
    GUI polls on its timer (no Qt signals -- see the module docstring).

    Single-use: construct one per dump."""

    def __init__(self, device: LogDevice) -> None:
        self.device = device
        self._lock = threading.Lock()
        self._bytes = 0
        self._done = False
        self._data: bytes = b""
        self._error: str | None = None
        self._cancel = threading.Event()
        self._thread = threading.Thread(target=self._run, name="logdump", daemon=True)

    def start(self) -> None:
        self._thread.start()

    def cancel(self) -> None:
        """Asks the read loop to stop. Whatever arrived so far is still
        decodable -- chunk boundaries are self-describing, and a torn
        trailing record is dropped by parse_chunks()."""
        self._cancel.set()

    @property
    def cancelled(self) -> bool:
        return self._cancel.is_set()

    def _run(self) -> None:
        try:
            data = self.device.dump(
                on_progress=self._note_progress, should_stop=self._cancel.is_set
            )
            with self._lock:
                self._data = data
        except Exception as e:  # noqa: BLE001 -- any failure is the GUI's to report
            with self._lock:
                self._error = str(e)
        finally:
            with self._lock:
                self._done = True

    def _note_progress(self, total: int) -> None:
        with self._lock:
            self._bytes = total

    def poll(self) -> tuple[bool, int, bytes, str | None]:
        """@returns (done, bytes_received, data, error). `data` is empty
        until done."""
        with self._lock:
            return self._done, self._bytes, self._data, self._error
