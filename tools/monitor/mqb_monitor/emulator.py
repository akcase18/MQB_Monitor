"""The fake car: answers a diagnostic dongle (OBDEleven) on the bench as if
it were the car's modules, so a value can be set and held while the phone
shows what it decodes to.

    dongle --OBD socket--> CAN <--> dash/sniffer (SLCAN, normal mode) <--> Mac

The answers come from, in order:

  * overrides -- per (module, DID) payloads set in the Fake Car tab: the
    signal being faked;
  * a recording -- a capture of the dongle talking to the real car, replayed
    request-for-request (the connect handshake, identification, sessions),
    with read-data answers taken as they were at a chosen moment (freeze_at);
  * defaults -- tester present and session control, so a module with no
    recording still holds a session open.

Anything else gets a negative response from a module that exists, and
silence from one that doesn't, like the real bus.

ISO-TP is done properly in both directions: a long answer goes out as a
first frame, then consecutive frames paced by the dongle's flow control
(block size, STmin); a long request is flow-controlled and reassembled.

SimDongle plays the dongle's side for --selftest and the GUI's Simulated
dongle source. No PySide6.
"""
from __future__ import annotations

import threading
import time
from collections import Counter
from dataclasses import dataclass, field
from typing import Callable

from .can_frame import CanFrame
from .uds import IsoTpReassembler

DEFAULT_PAD = 0xAA  # VW modules pad short frames with 0xAA
# rx - tx for the usual VW addressing: 7E0 -> 7E8 (OBD), 713 -> 77D (UDS).
RX_TX_DELTAS = (0x08, 0x6A)
# Functional (broadcast) request ids: OBD's 7DF, VW's 700.
FUNCTIONAL_IDS = (0x7DF, 0x700)
PAIR_WINDOW_S = 5.0
SID_NEGATIVE = 0x7F
NRC_RESPONSE_PENDING = 0x78
NRC_SERVICE_NOT_SUPPORTED = 0x11
NRC_INCORRECT_LENGTH = 0x13
NRC_OUT_OF_RANGE = 0x31
# Services whose response echoes a 2-byte identifier after the SID.
_ID_SERVICES = (0x22, 0x2E, 0x31)

Send = Callable[[CanFrame], None]


def _is_request(sdu: bytes) -> bool:
    sid = sdu[0]
    return sid < 0x40 or 0x80 <= sid < 0xC0


def _answers(request: bytes, response: bytes) -> bool:
    """True if `response` is an answer (positive or negative) to `request`."""
    sid = request[0]
    if response[0] == SID_NEGATIVE:
        return len(response) >= 3 and response[1] == sid
    if response[0] != (sid + 0x40) & 0xFF:
        return False
    if sid in _ID_SERVICES:
        return response[1:3] == request[1:3]
    return True


def stmin_seconds(b: int) -> float:
    """ISO-TP STmin byte -> seconds (0-127 ms, or 100-900 us)."""
    if b <= 0x7F:
        return b / 1000.0
    if 0xF1 <= b <= 0xF9:
        return (b - 0xF0) / 10000.0
    return 0.127  # reserved values: the spec says use the longest


def sdu_frames(sdu: bytes, pad: int = DEFAULT_PAD) -> list[bytes]:
    """`sdu` as ISO-TP frames: one single frame, or a first frame followed
    by consecutive frames. Each padded to 8 bytes."""
    def padded(b: bytes) -> bytes:
        return b + bytes([pad]) * (8 - len(b))

    if len(sdu) <= 7:
        return [padded(bytes([len(sdu)]) + sdu)]
    frames = [bytes([0x10 | (len(sdu) >> 8), len(sdu) & 0xFF]) + sdu[:6]]
    rest, seq = sdu[6:], 1
    while rest:
        frames.append(padded(bytes([0x20 | (seq & 0x0F)]) + rest[:7]))
        rest, seq = rest[7:], seq + 1
    return frames


@dataclass
class Exchange:
    """One request in a recording and every answer it got."""

    t: float
    tx: int
    request: bytes
    answers: list[tuple[int, bytes]] = field(default_factory=list)  # (rx, sdu)


@dataclass
class Background:
    """A frame the car sends on its own, over and over, whatever the dongle
    is doing -- a gateway heartbeat. The dongle may listen for it before it
    believes there's a car there."""

    can_id: int
    extended: bool
    data: bytes
    period: float  # seconds


# What makes a frame background rather than part of a conversation: this
# many sightings at least, spread over at least this share of the capture.
_BACKGROUND_MIN_COUNT = 5
_BACKGROUND_MIN_SPAN = 0.5
# Gaps shorter than this are frames that queued up and arrived together,
# not the frame's real period.
_BACKGROUND_MIN_GAP_S = 0.02


class Recording:
    """A capture of a dongle talking to the car, as request -> answer
    exchanges, plus the frames the car sends on its own (background). Built
    with from_frames() (frames from capture.read_capture)."""

    def __init__(self, exchanges: list[Exchange], pad: int = DEFAULT_PAD,
                 background: list[Background] | None = None) -> None:
        self.exchanges = exchanges
        self.pad = pad
        self.background = background or []
        self.duration = max((e.t for e in exchanges), default=0.0)
        # Module request id -> response id, from physically addressed pairs.
        self.modules: dict[int, int] = {}
        self._by_request: dict[tuple[int, bytes], list[Exchange]] = {}
        for e in exchanges:
            self._by_request.setdefault((e.tx, e.request), []).append(e)
            if e.tx not in FUNCTIONAL_IDS and e.answers:
                self.modules.setdefault(e.tx, e.answers[0][0])
        self.request_ids = {e.tx for e in exchanges}

    @classmethod
    def from_frames(cls, frames: list[tuple[float, CanFrame]]) -> "Recording":
        iso = IsoTpReassembler()
        open_reqs: list[Exchange] = []
        exchanges: list[Exchange] = []
        pads: Counter = Counter()
        for t, frame in frames:
            d = frame.data
            # Padding is learned from the car's single-frame answers only:
            # the dongle pads its requests its own way (OBDEleven: 0x00,
            # where VW modules use 0xAA) and usually sends more of them.
            if (d and d[0] >> 4 == 0 and len(d) == 8 and 1 <= (d[0] & 0x0F) < 7
                    and not _is_request(d[1:])):
                pads[d[-1]] += 1
            sdu = iso.feed(frame)
            if sdu is None or not sdu.data:
                continue
            data = sdu.data
            if _is_request(data):
                ex = Exchange(t, frame.can_id, data)
                exchanges.append(ex)
                open_reqs.append(ex)
                continue
            if data[0] == SID_NEGATIVE and len(data) >= 3 and data[2] == NRC_RESPONSE_PENDING:
                continue  # "still working on it" -- the real answer follows
            rx = frame.can_id
            candidates = [
                e for e in open_reqs
                if t - e.t <= PAIR_WINDOW_S and _answers(e.request, data)
            ]
            if not candidates:
                continue
            # The module at the usual offset wins, then a broadcast, then
            # the most recent.
            candidates.sort(key=lambda e: (
                rx - e.tx in RX_TX_DELTAS, e.tx in FUNCTIONAL_IDS, e.t))
            ex = candidates[-1]
            ex.answers.append((rx, data))
            if ex.tx not in FUNCTIONAL_IDS:
                open_reqs.remove(ex)
            open_reqs = [e for e in open_reqs if t - e.t <= PAIR_WINDOW_S]
        pad = pads.most_common(1)[0][0] if pads else DEFAULT_PAD
        return cls(exchanges, pad, _background(frames, exchanges))

    def answers(self, tx: int, request: bytes, at: float | None = None
                ) -> list[tuple[int, bytes]] | None:
        """What the car answered to exactly this request -- the last time it
        was asked at or before `at` (the first time if `at` is earlier; the
        last time if `at` is None). None if it was never asked."""
        seen = [e for e in self._by_request.get((tx, request), []) if e.answers]
        if not seen:
            return None
        if at is None:
            return seen[-1].answers
        before = [e for e in seen if e.t <= at]
        return (before[-1] if before else seen[0]).answers

    def did_payload(self, tx: int, did: tuple[int, int], at: float | None = None
                    ) -> bytes | None:
        """The data the car returned for a single-DID read, at `at` as in
        answers(); None if it never answered one positively."""
        got = self.answers(tx, bytes([0x22, *did]), at)
        for _, sdu in got or []:
            if sdu[0] == 0x62:
                return sdu[3:]
        return None


def _background(frames: list[tuple[float, CanFrame]],
                exchanges: list[Exchange]) -> list[Background]:
    """Frames repeated through the whole capture by something that isn't
    part of any request or answer. The dongle's own one-off traffic (its
    TP2.0 probe, say) is too short-lived to count."""
    if not frames:
        return []
    talking = set(FUNCTIONAL_IDS)
    for e in exchanges:
        talking.add(e.tx)
        talking.update(rx for rx, _ in e.answers)
    seen: dict[tuple[int, bool], list[tuple[float, bytes]]] = {}
    for t, f in frames:
        if f.can_id not in talking and f.direction != "tx":
            seen.setdefault((f.can_id, f.extended), []).append((t, f.data))
    duration = frames[-1][0] - frames[0][0]
    found = []
    for (can_id, extended), hits in seen.items():
        times = [t for t, _ in hits]
        if (len(hits) < _BACKGROUND_MIN_COUNT
                or times[-1] - times[0] < _BACKGROUND_MIN_SPAN * duration):
            continue
        gaps = sorted(b - a for a, b in zip(times, times[1:])
                      if b - a >= _BACKGROUND_MIN_GAP_S)
        if not gaps:
            continue
        data = Counter(d for _, d in hits).most_common(1)[0][0]
        found.append(Background(can_id, extended, data, gaps[len(gaps) // 2]))
    return found


SERVICE_NAMES = {
    0x10: "session control", 0x11: "ECU reset", 0x14: "clear DTCs",
    0x19: "read DTCs", 0x1A: "read ECU id (KWP)", 0x22: "read data",
    0x23: "read memory", 0x27: "security access", 0x28: "comms control",
    0x2E: "write data", 0x31: "routine", 0x3E: "tester present",
    0x85: "DTC setting",
}
# Services whose second byte (sub-function, or the session/level asked for)
# says what was actually done, so 10 01 and 10 03 are listed apart.
_SUBFUNCTION_SERVICES = (0x10, 0x11, 0x19, 0x27, 0x28, 0x31, 0x3E, 0x85)


def summarize(rec: Recording) -> list[str]:
    """What the dongle did to each module in a recording, as text lines:
    every service it used, in the order it first used it, how often, and
    how the module answered. Shows the connect handshake, and whether a
    module was put in a diagnostic session before being read."""
    by_tx: dict[int, list[Exchange]] = {}
    for e in rec.exchanges:
        by_tx.setdefault(e.tx, []).append(e)

    lines = [f"{len(rec.exchanges)} requests over {rec.duration:.1f} s"]
    sessioned = []
    for tx, exs in sorted(by_tx.items(), key=lambda kv: kv[1][0].t):
        if tx in FUNCTIONAL_IDS:
            rxs = sorted({rx for e in exs for rx, _ in e.answers})
            heard = ", ".join(f"{rx:03X}" for rx in rxs) or "nobody"
            lines.append(f"\nBroadcast {tx:03X} (answered by {heard})")
        else:
            rx = rec.modules.get(tx)
            lines.append(f"\nModule {tx:03X} -> {rx:03X}" if rx is not None
                         else f"\nModule {tx:03X} (never answered)")
        groups: dict[bytes, list[Exchange]] = {}
        for e in exs:
            sid = e.request[0]
            n = 2 if sid in _SUBFUNCTION_SERVICES else 1
            groups.setdefault(e.request[:n], []).append(e)
        for key, group in groups.items():
            name = SERVICE_NAMES.get(key[0], "?")
            if key[0] == 0x22:
                dids = {e.request[1:] for e in group}
                name += f" ({len(dids)} DID{'s' if len(dids) != 1 else ''})"
            ok = sum(1 for e in group if any(a[0] != SID_NEGATIVE for _, a in e.answers))
            nrcs = Counter(a[2] for e in group for _, a in e.answers
                           if a[0] == SID_NEGATIVE and len(a) >= 3)
            silent = sum(1 for e in group if not e.answers)
            how = [f"{ok} ok"] if ok else []
            how += [f"NRC {nrc:02X} x{n}" for nrc, n in sorted(nrcs.items())]
            if silent:
                how.append(f"{silent} unanswered")
            lines.append(f"  {group[0].t:7.1f} s  {key.hex(' '):6s} {name:24s} "
                         f"{len(group):5d}x  {', '.join(how)}")
        first_read = next((e.t for e in exs if e.request[0] == 0x22), None)
        first_session = next((e.t for e in exs if e.request[:1] == b"\x10"
                              and e.request[1:2] not in (b"", b"\x01")), None)
        if (tx not in FUNCTIONAL_IDS and first_read is not None
                and first_session is not None and first_session <= first_read):
            sessioned.append(f"{tx:03X}")
    lines.append("\nPut in a non-default session before its first read: "
                 + (", ".join(sessioned) if sessioned else "none"))
    return lines


@dataclass
class Override:
    """A faked DID: what `rx` answers to a read of `did` sent to `tx`."""

    tx: int
    rx: int
    did: tuple[int, int]
    payload: bytes


@dataclass
class Asked:
    """One distinct request the dongle sent, for the Fake Car tab's log."""

    tx: int
    request: bytes
    count: int = 0
    how: str = ""  # what the fake car did about it
    last: float = 0.0


@dataclass
class _Outgoing:
    rx: int
    frames: list[bytes]  # consecutive frames still to send
    sent_since_fc: int = 0


class FakeCar:
    """The emulator. Feed it every frame from the bus (on_frame); it sends
    its answers through `send`. Thread-safe: frames arrive on the source
    thread while the GUI changes overrides."""

    def __init__(self, send: Send) -> None:
        self.send = send
        self.recording: Recording | None = None
        self.freeze_at: float | None = None
        self.overrides: dict[tuple[int, tuple[int, int]], Override] = {}
        self.asked: dict[tuple[int, bytes], Asked] = {}
        self.answered = 0
        self._lock = threading.RLock()
        self._req_iso = IsoTpReassembler()
        self._outgoing: dict[int, _Outgoing] = {}  # by the dongle's tx id
        self._bg_stop: threading.Event | None = None

    # --- background frames (own thread while running) ---

    def start_background(self) -> None:
        """Starts repeating the recording's background frames (the car's
        heartbeat), each at its recorded period, until stop_background()."""
        self.stop_background()
        stop = self._bg_stop = threading.Event()
        threading.Thread(target=self._run_background, args=(stop,),
                         daemon=True).start()

    def stop_background(self) -> None:
        if self._bg_stop is not None:
            self._bg_stop.set()
            self._bg_stop = None

    def _run_background(self, stop: threading.Event) -> None:
        due: dict[int, float] = {}
        while not stop.is_set():
            rec = self.recording
            now = time.monotonic()
            wait = 0.05
            for b in rec.background if rec else []:
                if due.get(b.can_id, 0.0) <= now:
                    self.send(CanFrame(now, b.can_id, b.data,
                                       extended=b.extended, direction="tx"))
                    due[b.can_id] = now + b.period
                wait = min(wait, due[b.can_id] - now)
            stop.wait(max(wait, 0.001))

    # --- configuration (GUI thread) ---

    @property
    def pad(self) -> int:
        return self.recording.pad if self.recording else DEFAULT_PAD

    def modules(self) -> dict[int, int]:
        """Every module the fake car answers as: request id -> response id."""
        with self._lock:
            mods = dict(self.recording.modules) if self.recording else {}
            for o in self.overrides.values():
                mods.setdefault(o.tx, o.rx)
            return mods

    def set_override(self, tx: int, rx: int, did: tuple[int, int], payload: bytes) -> None:
        with self._lock:
            self.overrides[(tx, did)] = Override(tx, rx, did, bytes(payload))

    def remove_override(self, tx: int, did: tuple[int, int]) -> None:
        with self._lock:
            self.overrides.pop((tx, did), None)

    def payload(self, tx: int, did: tuple[int, int]) -> bytes | None:
        with self._lock:
            o = self.overrides.get((tx, did))
            return o.payload if o else None

    def targets(self) -> list[tuple[int, bytes]]:
        """A read request for every faked DID, for a simulated dongle."""
        with self._lock:
            return [(o.tx, bytes([0x22, *o.did])) for o in self.overrides.values()]

    def asked_list(self) -> list[Asked]:
        with self._lock:
            return [Asked(a.tx, a.request, a.count, a.how, a.last)
                    for a in self.asked.values()]

    def clear_log(self) -> None:
        with self._lock:
            self.asked.clear()
            self.answered = 0

    # --- the bus (source thread) ---

    def on_frame(self, frame: CanFrame) -> None:
        if frame.direction == "tx" or not frame.data:
            return  # our own answers
        out: list[tuple[int, bytes]] = []
        gap = 0.0
        with self._lock:
            tx = frame.can_id
            mods = self.modules()
            listening = tx in mods or (
                self.recording is not None and tx in self.recording.request_ids)
            if not listening:
                return
            pci = frame.data[0] >> 4
            if pci == 0x3:
                out, gap = self._flow_control(tx, frame.data)
            else:
                if pci == 0x1 and tx in mods:
                    # A long request: let it come (no block limit, no gap).
                    out.append((mods[tx], self._padded(b"\x30\x00\x00")))
                sdu = self._req_iso.feed(frame)
                if sdu is not None and sdu.data:
                    out += self._handle_request(tx, sdu.data, frame.timestamp)
        for i, (can_id, data) in enumerate(out):
            if i and gap:
                time.sleep(gap)
            self.send(CanFrame(time.monotonic(), can_id, data, direction="tx"))

    def _padded(self, b: bytes) -> bytes:
        return b + bytes([self.pad]) * (8 - len(b))

    def _handle_request(self, tx: int, req: bytes, ts: float) -> list[tuple[int, bytes]]:
        answers, how = self._answer(tx, req)
        entry = self.asked.get((tx, req))
        if entry is None:
            if len(self.asked) >= 500:
                self.asked.pop(next(iter(self.asked)))
            entry = self.asked[(tx, req)] = Asked(tx, req)
        entry.count += 1
        entry.how = how
        entry.last = ts
        frames: list[tuple[int, bytes]] = []
        for rx, sdu in answers:
            self.answered += 1
            parts = sdu_frames(sdu, self.pad)
            frames.append((rx, parts[0]))
            if len(parts) > 1:
                self._outgoing[tx] = _Outgoing(rx, parts[1:])
        return frames

    def _flow_control(self, tx: int, fc: bytes) -> tuple[list[tuple[int, bytes]], float]:
        pending = self._outgoing.get(tx)
        if pending is None:
            return [], 0.0
        status = fc[0] & 0x0F
        if status == 1:  # wait: another FC follows
            return [], 0.0
        if status != 0:  # overflow/abort
            del self._outgoing[tx]
            return [], 0.0
        block = fc[1] if len(fc) > 1 else 0
        gap = stmin_seconds(fc[2]) if len(fc) > 2 else 0.0
        n = len(pending.frames) if block == 0 else block
        burst, pending.frames = pending.frames[:n], pending.frames[n:]
        if not pending.frames:
            del self._outgoing[tx]
        return [(pending.rx, f) for f in burst], gap

    def _answer(self, tx: int, req: bytes) -> tuple[list[tuple[int, bytes]], str]:
        sid = req[0]
        rx = self.modules().get(tx)
        rec = self.recording

        if sid == 0x22 and rx is not None:
            ids = req[1:]
            dids = [(ids[i], ids[i + 1]) for i in range(0, len(ids) - 1, 2)]
            if not dids or len(ids) % 2:
                return [(rx, bytes([SID_NEGATIVE, sid, NRC_INCORRECT_LENGTH]))], "bad length"
            faked = any((tx, d) in self.overrides for d in dids)
            if not faked and rec is not None:
                got = rec.answers(tx, req, self.freeze_at)
                if got:
                    return got, "recording"
            body = bytearray([0x62])
            for d in dids:
                o = self.overrides.get((tx, d))
                data = o.payload if o else (
                    rec.did_payload(tx, d, self.freeze_at) if rec else None)
                if data is None:
                    return ([(rx, bytes([SID_NEGATIVE, sid, NRC_OUT_OF_RANGE]))],
                            "no data for this DID")
                body += bytes(d) + data
            return [(rx, bytes(body))], "faked" if faked else "recording"

        if rec is not None:
            got = rec.answers(tx, req, self.freeze_at)
            if got:
                return got, "recording"
        if rx is None:
            return [], "no such module"
        if sid == 0x3E:
            sub = req[1] if len(req) > 1 else 0
            if sub & 0x80:
                return [], "tester present (no reply asked)"
            return [(rx, bytes([0x7E, sub]))], "default"
        if sid == 0x10 and len(req) >= 2:
            # P2 50 ms, P2* 5 s -- the usual VW timing.
            return [(rx, bytes([0x50, req[1], 0x00, 0x32, 0x01, 0xF4]))], "default"
        return ([(rx, bytes([SID_NEGATIVE, sid, NRC_SERVICE_NOT_SUPPORTED]))],
                "not supported")


@dataclass
class Sweep:
    """Steps a raw value from `start` towards `stop` by `step`, one step per
    advance(); stays at the end once there."""

    start: int
    stop: int
    step: int = 1
    current: int | None = None

    def advance(self, direction: int = 1) -> int:
        step = abs(self.step) or 1
        if self.stop < self.start:
            step = -step
        if self.current is None:
            self.current = self.start
            return self.current
        lo, hi = sorted((self.start, self.stop))
        self.current = max(lo, min(hi, self.current + direction * step))
        return self.current

    @property
    def done(self) -> bool:
        return self.current == self.stop


class SimDongle:
    """Plays the dongle's part with no hardware: sends requests, answers
    first frames with flow control, and keeps every complete answer. Its
    frames go out through `send`; the fake car's come in through
    on_frame()."""

    def __init__(self, send: Send, block_size: int = 0, stmin: int = 0) -> None:
        self.send = send
        self.block_size = block_size
        self.stmin = stmin
        self.answers: list[tuple[int, bytes]] = []  # (rx, sdu), arrival order
        self._iso = IsoTpReassembler()
        self._last_tx: int | None = None
        self._cf_since_fc = 0

    def request(self, tx: int, sdu: bytes) -> None:
        self._last_tx = tx
        for f in sdu_frames(sdu, 0x55):
            self.send(CanFrame(time.monotonic(), tx, f))

    def on_frame(self, frame: CanFrame) -> None:
        if not frame.data or frame.can_id == self._last_tx:
            return
        pci = frame.data[0] >> 4
        if pci == 0x1:
            self._fc()
        elif pci == 0x2 and self.block_size:
            self._cf_since_fc += 1
            if self._cf_since_fc == self.block_size:
                self._fc()
        sdu = self._iso.feed(frame)
        if sdu is not None:
            self.answers.append((frame.can_id, sdu.data))

    def _fc(self) -> None:
        self._cf_since_fc = 0
        if self._last_tx is not None:
            fc = bytes([0x30, self.block_size, self.stmin]) + bytes([0x55] * 5)
            self.send(CanFrame(time.monotonic(), self._last_tx, fc))
