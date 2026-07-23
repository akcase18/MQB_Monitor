"""Frame sources: where CanFrames come from. All run a background thread and
push frames to an `on_frame(CanFrame)` callback; they are Qt-agnostic so the
engine can be exercised headless.

  * SlcanSerialSource -- the real thing: drives the Teensy sniffer
    (env:can_sniffer) over USB using the SLCAN handshake, listen-only.
  * DemoSource -- synthesizes plausible OBDEleven-style scan traffic from the
    catalog (including one unknown DID and one multi-frame response) so the UI
    and decode/learn paths can be developed with no hardware attached.
"""
from __future__ import annotations

import math
import threading
import time
from typing import Callable

import serial
from serial.tools import list_ports

from .can_frame import CanFrame
from .catalog import Catalog

OnFrame = Callable[[CanFrame], None]

OBD_TX_ID = 0x7E0  # tester -> ECU (requests)
OBD_RX_ID = 0x7E8  # ECU -> tester (responses)

# SLCAN bitrate index for 500 kbit/s (the MQB powertrain rate).
SLCAN_500K = "S6"


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


class SlcanSerialSource:
    """Reads SLCAN frames from the Teensy sniffer over a serial port."""

    def __init__(
        self,
        port: str,
        on_frame: OnFrame,
        listen_only: bool = True,
        baud: int = 115200,
    ) -> None:
        self.port = port
        self.on_frame = on_frame
        self.listen_only = listen_only
        self.baud = baud
        self._ser: serial.Serial | None = None
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()

    def start(self) -> None:
        self._ser = serial.Serial(self.port, self.baud, timeout=0.1)
        # Handshake: close (in case it was left open), set bitrate, open.
        self._ser.write(b"C\r")
        self._ser.write((SLCAN_500K + "\r").encode())
        self._ser.write(b"L\r" if self.listen_only else b"O\r")
        self._ser.reset_input_buffer()
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=1.0)
        if self._ser:
            try:
                self._ser.write(b"C\r")
                self._ser.close()
            except serial.SerialException:
                pass
            self._ser = None

    def _run(self) -> None:
        assert self._ser is not None
        buf = bytearray()
        while not self._stop.is_set():
            try:
                chunk = self._ser.read(256)
            except serial.SerialException:
                break
            if not chunk:
                continue
            buf.extend(chunk)
            while b"\r" in buf:
                line, _, rest = buf.partition(b"\r")
                buf = bytearray(rest)
                frame = parse_slcan_line(bytes(line))
                if frame is not None:
                    self.on_frame(frame)


def parse_slcan_line(line: bytes) -> CanFrame | None:
    """Parses one SLCAN 't'/'T' data-frame line (without the trailing CR).
    Any trailing timestamp is ignored -- we host-timestamp on arrival."""
    if not line:
        return None
    kind = line[0:1]
    if kind not in (b"t", b"T"):
        return None  # command echo / status reply / remote frame -- skip
    extended = kind == b"T"
    id_len = 8 if extended else 3
    try:
        s = line.decode("ascii")
    except UnicodeDecodeError:
        return None
    if len(s) < 1 + id_len + 1:
        return None
    try:
        can_id = int(s[1 : 1 + id_len], 16)
        dlc = int(s[1 + id_len : 1 + id_len + 1], 16)
    except ValueError:
        return None
    data_start = 1 + id_len + 1
    data_hex = s[data_start : data_start + dlc * 2]
    if len(data_hex) < dlc * 2:
        return None
    try:
        data = bytes.fromhex(data_hex)
    except ValueError:
        return None
    return CanFrame(
        timestamp=time.monotonic(),
        can_id=can_id,
        data=data,
        extended=extended,
    )


class DemoSource:
    """Synthesizes OBDEleven-style scan traffic from the catalog, no hardware.

    Walks the catalog's DIDs round-robin, emitting a request frame on 0x7E0
    and a matching response on 0x7E8 with a value that drifts over time (so the
    UI looks alive). Periodically injects an UNKNOWN DID (for learn mode) and a
    multi-frame response (to exercise ISO-TP reassembly)."""

    UNKNOWN_DID = (0x24, 0x11)  # not in the catalog on purpose
    MULTIFRAME_DID = (0xF1, 0x90)  # pretend VIN-ish long response

    def __init__(
        self, catalog: Catalog, on_frame: OnFrame, period_s: float = 0.02
    ) -> None:
        self.catalog = catalog
        self.on_frame = on_frame
        self.period_s = period_s
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()
        # A flat schedule of DIDs to poll, plus the synthetic extras.
        self._dids: list[tuple[int, int]] = []
        for sig in catalog.signals:
            for req in sig.requests:
                if req.did not in self._dids:
                    self._dids.append(req.did)
        self._dids.append(self.UNKNOWN_DID)
        self._dids.append(self.MULTIFRAME_DID)
        self._i = 0

    def start(self) -> None:
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=1.0)

    def _run(self) -> None:
        while not self._stop.is_set():
            did = self._dids[self._i % len(self._dids)]
            self._i += 1
            self._emit_exchange(did)
            time.sleep(self.period_s)

    def _emit_exchange(self, did: tuple[int, int]) -> None:
        now = time.monotonic()
        # Request: [0x03, 0x22, b1, b2, pad...]
        req = bytes([0x03, 0x22, did[0], did[1], 0, 0, 0, 0])
        self.on_frame(CanFrame(now, OBD_TX_ID, req, direction="rx"))

        if did == self.MULTIFRAME_DID:
            self._emit_multiframe(did)
            return

        payload = self._payload_for(did, now)
        length = 1 + 2 + len(payload)  # service + DID + payload
        resp = bytes([length, 0x62, did[0], did[1]]) + payload
        resp = resp.ljust(8, b"\x00")
        self.on_frame(CanFrame(time.monotonic(), OBD_RX_ID, resp, direction="rx"))

    def _payload_for(self, did: tuple[int, int], now: float) -> bytes:
        if did == self.UNKNOWN_DID:
            # A believable but uncatalogued 2-byte value.
            val = int(2000 + 1500 * (0.5 + 0.5 * math.sin(now / 3.0)))
            return bytes([(val >> 8) & 0xFF, val & 0xFF])
        # For a known DID, produce a value near the middle of its first
        # signal's display range, decoded back through offset/scale so the
        # tool shows something sensible.
        pairs = self.catalog.signals_for_did(did)
        if pairs:
            sig, idx = pairs[0]
            req = sig.requests[idx]
            mid = (sig.display_min + sig.display_max) / 2.0
            span = (sig.display_max - sig.display_min) / 2.0
            phase = did[0] + did[1]
            disp = mid + 0.6 * span * math.sin(now / 2.0 + phase)
            raw = disp / (req.scale or 1.0) - req.offset
            raw_i = max(-32768, min(32767, int(round(raw))))
            return bytes([(raw_i >> 8) & 0xFF, raw_i & 0xFF])
        return bytes([0x00, 0x00])

    def _emit_multiframe(self, did: tuple[int, int]) -> None:
        # A 17-byte "VIN-like" payload split across a First + Consecutive
        # frames, so the reassembler has real work to do.
        sdu = bytes([0x62, did[0], did[1]]) + b"WVWZZZ1KZAW000000"[:17]
        total = len(sdu)
        # First frame: PCI 0x1, 12-bit length, then first 6 SDU bytes.
        ff = bytes([0x10 | ((total >> 8) & 0x0F), total & 0xFF]) + sdu[:6]
        self.on_frame(CanFrame(time.monotonic(), OBD_RX_ID, ff, direction="rx"))
        # Consecutive frames: PCI 0x2 + sequence number, 7 bytes each.
        rest = sdu[6:]
        seq = 1
        while rest:
            block = rest[:7]
            rest = rest[7:]
            cf = bytes([0x20 | (seq & 0x0F)]) + block
            cf = cf.ljust(8, b"\x00")
            self.on_frame(CanFrame(time.monotonic(), OBD_RX_ID, cf, direction="rx"))
            seq += 1
