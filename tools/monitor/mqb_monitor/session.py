"""The monitor engine: consumes CanFrames, reassembles ISO-TP, parses UDS,
decodes known signals against the catalog, and tracks unknown DIDs for learn
mode. GUI-agnostic and thread-safe -- a source thread calls feed() while the
GUI polls snapshot() on a timer.
"""
from __future__ import annotations

import threading
import time
from collections import OrderedDict, deque
from dataclasses import dataclass, field

from .can_frame import CanFrame
from .catalog import Catalog, Ecu, Signal, decode_raw_payload
from .uds import NRC_NAMES, IsoTpReassembler, UdsMessage, parse_uds

# Generous caps, not tuned-to-the-byte ones -- both are small dataclasses (a
# few ints/floats/short strings), so even a few hundred thousand of them is a
# few tens of MB, trivial for a desktop tool. Sized this high specifically so
# "save the whole session's log" (gui.py's Save Log) covers an entire bench
# session rather than just the last minute or two of traffic.
MAX_FRAME_LOG = 200_000
MAX_MESSAGE_LOG = 200_000
# Distinct raw payloads kept per unknown DID (and saved with a WIP signal).
# A value that never changes is one sample however often it's polled.
MAX_DISTINCT_SAMPLES = 50
# How long after a request its response may arrive and still be paired with
# it (UDS allows a module to stall up to 5 s with "response pending").
REQUEST_PAIR_WINDOW_S = 5.0
# rx - tx for the usual VW addressing: 7E0 -> 7E8 (OBD), 713 -> 77D (UDS).
_RX_TX_DELTAS = (0x08, 0x6A)


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


# Unknown DIDs are keyed by (response CAN id, DID): the same DID number in
# two modules is two different things.
UnknownKey = tuple[int, tuple[int, int]]


@dataclass
class UnknownDid:
    did: tuple[int, int]
    # The module that answered: rx is the response's CAN id, tx the id its
    # request went out on (None until a request is seen, e.g. a capture
    # that started mid-exchange).
    rx_id: int
    tx_id: int | None = None
    count: int = 0
    last_seen: float = 0.0
    last: bytes = b""
    # payload -> times seen, least recently seen first.
    distinct: OrderedDict[bytes, int] = field(default_factory=OrderedDict)

    @property
    def key(self) -> UnknownKey:
        return (self.rx_id, self.did)

    @property
    def ecu(self) -> Ecu | None:
        return None if self.tx_id is None else Ecu(self.tx_id, self.rx_id)

    @property
    def samples(self) -> list[bytes]:
        """The distinct payloads, least recently seen first."""
        return list(self.distinct)

    def add(self, payload: bytes, ts: float) -> None:
        self.count += 1
        self.last_seen = ts
        self.last = payload
        self.distinct[payload] = self.distinct.pop(payload, 0) + 1
        while len(self.distinct) > MAX_DISTINCT_SAMPLES:
            self.distinct.popitem(last=False)

    def copy(self) -> "UnknownDid":
        return UnknownDid(self.did, self.rx_id, self.tx_id, self.count,
                          self.last_seen, self.last, OrderedDict(self.distinct))


@dataclass
class Snapshot:
    frames: list[tuple[CanFrame, str]]
    messages: list[DecodedMessage]
    readings: dict[int, SignalReading]
    unknown: dict[UnknownKey, UnknownDid]
    total_frames: int
    started_at: float

    def responders(self, did: tuple[int, int]) -> list[UnknownDid]:
        """Every module seen answering `did`, by response id."""
        return sorted((u for u in self.unknown.values() if u.did == did),
                      key=lambda u: u.rx_id)


def frame_note(frame: CanFrame) -> str:
    """The ISO-TP frame type, for multi-frame traffic in logs."""
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


class MonitorEngine:
    def __init__(self, catalog: Catalog) -> None:
        self.catalog = catalog
        self._lock = threading.Lock()
        self._seq = 0
        self._clear()

    def _clear(self, started_at: float | None = None) -> None:
        self._iso = IsoTpReassembler()
        self._frames: deque[tuple[CanFrame, str]] = deque(maxlen=MAX_FRAME_LOG)
        self._messages: deque[DecodedMessage] = deque(maxlen=MAX_MESSAGE_LOG)
        self._readings: dict[int, SignalReading] = {
            s.id: SignalReading(s.id, s.name, s.unit) for s in self.catalog.signals
        }
        self._unknown: dict[UnknownKey, UnknownDid] = {}
        # (request CAN id, DID) -> when it went out, until answered.
        self._pending_req: dict[tuple[int, tuple[int, int]], float] = {}
        # (response CAN id, DID) -> newest payload and when, known or not.
        self._latest: dict[UnknownKey, tuple[bytes, float]] = {}
        self._total = 0
        self._started = time.monotonic() if started_at is None else started_at

    def reset(self, started_at: float | None = None) -> None:
        """Forgets all traffic, e.g. before replaying a capture.
        `started_at` is the monotonic time the Traffic tab counts from.
        Message sequence numbers keep counting, so views that track them
        don't mistake new messages for ones already shown."""
        with self._lock:
            self._clear(started_at)

    def _add_message(self, **kwargs) -> None:
        self._seq += 1
        self._messages.append(DecodedMessage(seq=self._seq, **kwargs))

    # --- ingest (source thread) ---

    def feed(self, frame: CanFrame) -> None:
        with self._lock:
            self._total += 1
            self._frames.append((frame, frame_note(frame)))

            sdu = self._iso.feed(frame)
            if sdu is None:
                return
            msg = parse_uds(sdu)
            if msg is None:
                return
            self._handle_uds(msg)

    def _handle_uds(self, msg: UdsMessage) -> None:
        if msg.kind == "request":
            self._pending_req[(msg.can_id, msg.did)] = msg.timestamp
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
        tx_id = self._match_request(msg)
        if tx_id is not None:
            req_ts = self._pending_req.pop((tx_id, msg.did))
            latency = (msg.timestamp - req_ts) * 1000.0

        self._latest[(msg.can_id, msg.did)] = (msg.payload, msg.timestamp)
        pairs = self.catalog.signals_for_did(msg.did, rx_id=msg.can_id)
        if pairs:
            self._decode_known(msg, pairs, latency)
        else:
            self._record_unknown(msg, latency, tx_id)

    def _match_request(self, msg: UdsMessage) -> int | None:
        """The CAN id of the pending request `msg` answers. When several
        modules were asked for the same DID at once, the one at the usual
        VW offset from the response id wins, then the most recent."""
        recent = [
            (tx, ts) for (tx, did), ts in self._pending_req.items()
            if did == msg.did
            and 0 <= msg.timestamp - ts <= REQUEST_PAIR_WINDOW_S
        ]
        if not recent:
            return None
        recent.sort(key=lambda p: (msg.can_id - p[0] in _RX_TX_DELTAS, p[1]))
        return recent[-1][0]

    def _decode_known(
        self,
        msg: UdsMessage,
        pairs: list[tuple[Signal, int]],
        latency: float | None,
    ) -> None:
        notes: list[str] = []
        for sig, idx in pairs:
            value = sig.requests[idx].decode(msg.payload)
            if value is None:
                notes.append(f"{sig.name} (payload too short)")
                continue
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
                shown = (
                    sig.enum_label(reading.value) if sig.unit == "Enum"
                    else f"{reading.value:.3g}"
                )
                notes.append(f"{sig.name} = {shown}")
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

    def _record_unknown(
        self, msg: UdsMessage, latency: float | None, tx_id: int | None
    ) -> None:
        key = (msg.can_id, msg.did)
        entry = self._unknown.get(key)
        if entry is None:
            entry = UnknownDid(did=msg.did, rx_id=msg.can_id)
            self._unknown[key] = entry
        if tx_id is not None:
            entry.tx_id = tx_id
        entry.add(msg.payload, msg.timestamp)

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
                unknown={k: u.copy() for k, u in self._unknown.items()},
                total_frames=self._total,
                started_at=self._started,
            )

    def latest(
        self, did: tuple[int, int], rx_id: int | None = None
    ) -> tuple[bytes, float, int] | None:
        """The newest response payload for `did` -- (payload, monotonic
        timestamp, response CAN id) -- from module `rx_id`, or with no
        `rx_id` from the only module answering it. None when nothing (or
        more than one module, with no way to tell which) has answered."""
        with self._lock:
            hits = [
                (rx, p, ts) for (rx, d), (p, ts) in self._latest.items()
                if d == did and (rx_id is None or rx == rx_id)
            ]
        if len(hits) != 1:
            return None
        rx, payload, ts = hits[0]
        return payload, ts, rx

    def refresh_catalog(self) -> None:
        """Re-syncs engine state after a learn-mode write added a signal:
        adds a reading row and stops treating that DID as unknown."""
        with self._lock:
            for s in self.catalog.signals:
                self._readings.setdefault(
                    s.id, SignalReading(s.id, s.name, s.unit)
                )
            for key in list(self._unknown.keys()):
                rx_id, did = key
                if self.catalog.signals_for_did(did, rx_id=rx_id):
                    del self._unknown[key]


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


def fill_wip_ecus(catalog: Catalog, unknown: dict[UnknownKey, UnknownDid]) -> list[Signal]:
    """Fills in the ECU of every WIP request that doesn't have one yet, from
    traffic: when exactly one module has been seen answering its DID and
    that module's request id is known too. A DID answered by several modules
    is left for the editor to ask about. Returns the signals changed; the
    caller saves."""
    by_did: dict[tuple[int, int], list[UnknownDid]] = {}
    for u in unknown.values():
        by_did.setdefault(u.did, []).append(u)
    changed = []
    for sig in catalog.signals:
        if not sig.is_wip:
            continue
        hit = False
        for req in sig.requests:
            seen = by_did.get(req.did, [])
            if req.ecu is None and len(seen) == 1 and seen[0].ecu is not None:
                req.ecu = seen[0].ecu
                hit = True
        if hit:
            changed.append(sig)
    return changed
