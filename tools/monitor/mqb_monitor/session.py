"""The monitor engine: consumes CanFrames, reassembles ISO-TP, parses UDS,
decodes known signals against the catalog, and tracks unknown DIDs for learn
mode. GUI-agnostic and thread-safe -- a source thread calls feed() while the
GUI polls snapshot() on a timer.
"""
from __future__ import annotations

import threading
import time
from collections import deque
from dataclasses import dataclass, field

from .can_frame import CanFrame
from .catalog import Catalog, Signal, decode_raw_payload
from .uds import NRC_NAMES, IsoTpReassembler, UdsMessage, parse_uds

# Generous caps, not tuned-to-the-byte ones -- both are small dataclasses (a
# few ints/floats/short strings), so even a few hundred thousand of them is a
# few tens of MB, trivial for a desktop tool. Sized this high specifically so
# "save the whole session's log" (gui.py's Save Log) covers an entire bench
# session rather than just the last minute or two of traffic.
MAX_FRAME_LOG = 200_000
MAX_MESSAGE_LOG = 200_000
MAX_UNKNOWN_SAMPLES = 20  # raw payloads retained per unknown DID for learn mode


@dataclass
class DecodedMessage:
    """A UDS message annotated with catalog decode results for display."""

    seq: int  # monotonic id, so a view can append only rows it hasn't shown
    timestamp: float
    kind: str  # request | response | negative
    can_id: int
    did: tuple[int, int]
    note: str  # human-readable summary
    known: bool
    latency_ms: float | None = None
    n_frames: int = 1


@dataclass
class SignalReading:
    signal_id: int
    name: str
    unit: str
    value: float | None = None  # metric/native value after transform
    last_update: float | None = None
    latency_ms: float | None = None
    updates: int = 0
    # latest per-request raw value, indexed by request position
    request_raw: dict[int, float] = field(default_factory=dict)


@dataclass
class UnknownDid:
    did: tuple[int, int]
    count: int = 0
    last_seen: float = 0.0
    samples: deque[bytes] = field(
        default_factory=lambda: deque(maxlen=MAX_UNKNOWN_SAMPLES)
    )


@dataclass
class Snapshot:
    frames: list[tuple[CanFrame, str]]
    messages: list[DecodedMessage]
    readings: dict[int, SignalReading]
    unknown: dict[tuple[int, int], UnknownDid]
    total_frames: int
    started_at: float


class MonitorEngine:
    def __init__(self, catalog: Catalog) -> None:
        self.catalog = catalog
        self._iso = IsoTpReassembler()
        self._lock = threading.Lock()

        self._frames: deque[tuple[CanFrame, str]] = deque(maxlen=MAX_FRAME_LOG)
        self._messages: deque[DecodedMessage] = deque(maxlen=MAX_MESSAGE_LOG)
        self._readings: dict[int, SignalReading] = {
            s.id: SignalReading(s.id, s.name, s.unit) for s in catalog.signals
        }
        self._unknown: dict[tuple[int, int], UnknownDid] = {}
        self._pending_req: dict[tuple[int, int], float] = {}
        self._total = 0
        self._seq = 0
        self._started = time.monotonic()

    def _add_message(self, **kwargs) -> None:
        self._seq += 1
        self._messages.append(DecodedMessage(seq=self._seq, **kwargs))

    # --- ingest (source thread) ---

    def feed(self, frame: CanFrame) -> None:
        with self._lock:
            self._total += 1
            self._frames.append((frame, self._frame_note(frame)))

            sdu = self._iso.feed(frame)
            if sdu is None:
                return
            msg = parse_uds(sdu)
            if msg is None:
                return
            self._handle_uds(msg)

    def _frame_note(self, frame: CanFrame) -> str:
        if not frame.data:
            return ""
        pci = frame.data[0] >> 4
        if pci == 0x1:
            return "ISO-TP first frame"
        if pci == 0x2:
            return f"ISO-TP consecutive #{frame.data[0] & 0x0F}"
        if pci == 0x3:
            return "ISO-TP flow control"
        return ""

    def _handle_uds(self, msg: UdsMessage) -> None:
        if msg.kind == "request":
            self._pending_req[msg.did] = msg.timestamp
            self._add_message(
                timestamp=msg.timestamp,
                kind="request",
                can_id=msg.can_id,
                did=msg.did,
                note=f"Request DID {msg.did_str}",
                known=msg.did in self.catalog.known_dids(),
                n_frames=msg.n_frames,
            )
            return

        if msg.kind == "negative":
            nrc = msg.nrc or 0
            self._add_message(
                timestamp=msg.timestamp,
                kind="negative",
                can_id=msg.can_id,
                did=(0, 0),
                note=f"Negative response (0x22): "
                f"{NRC_NAMES.get(nrc, f'NRC 0x{nrc:02X}')}",
                known=False,
                n_frames=msg.n_frames,
            )
            return

        # response
        latency = None
        req_ts = self._pending_req.pop(msg.did, None)
        if req_ts is not None:
            latency = (msg.timestamp - req_ts) * 1000.0

        pairs = self.catalog.signals_for_did(msg.did)
        if pairs:
            self._decode_known(msg, pairs, latency)
        else:
            self._record_unknown(msg, latency)

    def _decode_known(
        self,
        msg: UdsMessage,
        pairs: list[tuple[Signal, int]],
        latency: float | None,
    ) -> None:
        raw = decode_raw_payload(msg.payload)
        notes: list[str] = []
        for sig, idx in pairs:
            req = sig.requests[idx]
            value = (raw + req.offset) * req.scale
            reading = self._readings[sig.id]
            reading.request_raw[idx] = value
            reading.latency_ms = latency
            # Run the transform once every request in this signal has a value.
            if len(reading.request_raw) >= len(sig.requests):
                inputs = [
                    reading.request_raw.get(i, 0.0)
                    for i in range(len(sig.requests))
                ]
                reading.value = sig.transform_fn()(inputs)
                reading.last_update = msg.timestamp
                reading.updates += 1
                notes.append(f"{sig.name} = {reading.value:.3g}")
            else:
                notes.append(f"{sig.name} (req {idx + 1}/{len(sig.requests)})")

        self._add_message(
            timestamp=msg.timestamp,
            kind="response",
            can_id=msg.can_id,
            did=msg.did,
            note="; ".join(notes),
            known=True,
            latency_ms=latency,
            n_frames=msg.n_frames,
        )

    def _record_unknown(self, msg: UdsMessage, latency: float | None) -> None:
        entry = self._unknown.get(msg.did)
        if entry is None:
            entry = UnknownDid(did=msg.did)
            self._unknown[msg.did] = entry
        entry.count += 1
        entry.last_seen = msg.timestamp
        entry.samples.append(msg.payload)

        raw = decode_raw_payload(msg.payload)
        self._add_message(
            timestamp=msg.timestamp,
            kind="response",
            can_id=msg.can_id,
            did=msg.did,
            note=f"UNKNOWN DID {msg.did_str}  raw={raw:.0f}  "
            f"[{msg.payload.hex(' ')}]",
            known=False,
            latency_ms=latency,
            n_frames=msg.n_frames,
        )

    # --- read (GUI thread) ---

    def snapshot(self) -> Snapshot:
        with self._lock:
            return Snapshot(
                frames=list(self._frames),
                messages=list(self._messages),
                readings={
                    k: _copy_reading(v) for k, v in self._readings.items()
                },
                unknown=dict(self._unknown),
                total_frames=self._total,
                started_at=self._started,
            )

    def refresh_catalog(self) -> None:
        """Re-syncs engine state after a learn-mode write added a signal:
        adds a reading row and stops treating that DID as unknown."""
        with self._lock:
            for s in self.catalog.signals:
                self._readings.setdefault(
                    s.id, SignalReading(s.id, s.name, s.unit)
                )
            known = self.catalog.known_dids()
            for did in list(self._unknown.keys()):
                if did in known:
                    del self._unknown[did]


def _copy_reading(r: SignalReading) -> SignalReading:
    return SignalReading(
        signal_id=r.signal_id,
        name=r.name,
        unit=r.unit,
        value=r.value,
        last_update=r.last_update,
        latency_ms=r.latency_ms,
        updates=r.updates,
        request_raw=dict(r.request_raw),
    )
