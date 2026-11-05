"""Loads and decodes against core/vehicle/data/signals.json -- the two-stage
signal registry shared with the firmware.

Every signal ever defined lives in this one file, tagged with a `status`:
"active" (compiled into the firmware by tools/gen_catalog.py and offered on
the dash), "internal" (compiled in for the firmware's own use -- Dim Ctrl
drives auto brightness -- but never offered as a panel), or "wip" (still
being figured out -- offset/scale may be a guess, name may be a
placeholder). Catalog mirrors that split: signals_for_did/known_dids only
ever resolve compiled (active or internal) signals (see Catalog._reindex),
so the live monitor keeps treating a WIP signal's DID as "unknown, log it
raw" until it's promoted -- find_by_did() is the any-status escape hatch for
the Registry/Learn/WIP tabs, which need to see everything.

Decoding here MIRRORS the firmware exactly (core/can/src/uds_client.cpp +
core/vehicle/src/signal_store.cpp) so the values shown in this tool match what
the dashboard would show:

  * A request with no explicit `field` decodes the legacy way: a UDS
    response payload of 1 byte is an UNSIGNED 8-bit value; a payload of >=2
    bytes is a SIGNED big-endian 16-bit value (first two bytes). This
    signedness is load-bearing -- e.g. timing correction goes negative.
  * A request WITH a `field` reads `len` bytes from `start`, big-endian
    unless `little_endian` (optionally narrowed to a bit sub-field) -- how several signals pick
    their own value out of one multi-value DID. See decode_field().
  * Each request's raw value is (decoded + offset) * scale.
  * A signal's displayed value is transform(list of its requests' raw values).

The firmware decodes both forms (core::uds_decode_field). It only polls the
engine ECU so far, and tools/gen_catalog.py refuses to compile an active
signal it couldn't get an answer for.
"""
from __future__ import annotations

import json
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

# Repo layout: tools/monitor/mqb_monitor/catalog.py -> repo root is 3 up.
REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_SIGNALS_JSON = REPO_ROOT / "core/vehicle/data/signals.json"


@dataclass(frozen=True)
class Ecu:
    """The CAN ids a request is sent on (`tx`) and answered on (`rx`) --
    which module a DID belongs to. The same DID number can mean different
    things in different modules, so a request is only fully identified by
    (ecu, DID)."""

    tx: int
    rx: int

    def label(self) -> str:
        return f"{self.tx:03X}/{self.rx:03X}"


ENGINE_ECU = Ecu(0x7E0, 0x7E8)


@dataclass(frozen=True)
class Field:
    """Where a request's value sits in the response payload. The default
    (length 0) is the legacy auto decode; see decode_field()."""

    start: int = 0
    length: int = 0  # bytes; 0 = legacy auto
    signed: bool = False
    bit_shift: int = 0  # sub-field: shift right this many bits...
    bit_len: int = 0  # ...then keep this many; 0 = the whole field
    # Most VW data is big-endian, but not all: some DIDs send the low byte
    # first. Only matters for fields of 2+ bytes.
    little_endian: bool = False

    @property
    def is_auto(self) -> bool:
        return self.length == 0

    @property
    def byteorder(self) -> str:
        return "little" if self.little_endian else "big"

    def describe(self) -> str:
        if self.is_auto:
            return "auto"
        kind = "s" if self.signed else "u"
        text = f"{kind}{self.length * 8}"
        if self.little_endian and self.length > 1:
            text += " LE"
        text += f" @ byte {self.start}"
        if self.bit_len:
            text += f", bits {self.bit_shift}+{self.bit_len}"
        return text


AUTO_FIELD = Field()


def decode_field(payload: bytes, fld: Field = AUTO_FIELD) -> float | None:
    """Decodes one request's value from `payload` -- the bytes AFTER the 0x62
    service byte and the 2 DID bytes. Returns None when the payload is too
    short to hold `fld`.

    The auto field mirrors UdsClient::poll()'s decode (u8 for a 1-byte
    payload, else s16 from the first two bytes)."""
    if fld.is_auto:
        if len(payload) == 0:
            return 0.0
        if len(payload) == 1:
            return float(payload[0])  # unsigned 8-bit
        v = (payload[0] << 8) | payload[1]  # signed 16-bit big-endian
        if v >= 0x8000:
            v -= 0x10000
        return float(v)

    end = fld.start + fld.length
    if end > len(payload):
        return None
    v = int.from_bytes(payload[fld.start:end], fld.byteorder)
    width = fld.length * 8
    if fld.bit_len:
        v = (v >> fld.bit_shift) & ((1 << fld.bit_len) - 1)
        width = fld.bit_len
    if fld.signed and v >= 1 << (width - 1):
        v -= 1 << width
    return float(v)


def concrete_field(fld: Field, payload_len: int) -> Field:
    """`fld` with the legacy auto decode spelled out for a payload of
    `payload_len` bytes: s16 @ byte 0, or u8 for a 1-byte payload."""
    if not fld.is_auto:
        return fld
    return Field(0, 1) if payload_len == 1 else Field(0, 2, signed=True)


def field_range(fld: Field, payload_len: int) -> tuple[int, int]:
    """The smallest and largest raw value `fld` can hold."""
    fld = concrete_field(fld, payload_len)
    bits = fld.bit_len or fld.length * 8
    if fld.signed:
        return -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    return 0, (1 << bits) - 1


def encode_field(payload: bytes, fld: Field, raw: int) -> bytes:
    """`payload` with `fld` set to `raw` -- decode_field() run backwards, for
    the fake car. `raw` is clamped to what the field holds; bytes outside
    the field (and other bits of a bit sub-field) are kept, and a payload
    too short for the field is padded with zeros first."""
    fld = concrete_field(fld, len(payload) or 2)
    lo, hi = field_range(fld, len(payload))
    raw = max(lo, min(hi, int(raw)))
    buf = bytearray(payload)
    end = fld.start + fld.length
    if len(buf) < end:
        buf.extend(bytes(end - len(buf)))
    width = fld.bit_len or fld.length * 8
    bits = raw & ((1 << width) - 1)
    whole = int.from_bytes(buf[fld.start:end], fld.byteorder)
    if fld.bit_len:
        mask = ((1 << fld.bit_len) - 1) << fld.bit_shift
        whole = (whole & ~mask) | (bits << fld.bit_shift)
    else:
        whole = bits
    buf[fld.start:end] = whole.to_bytes(fld.length, fld.byteorder)
    return bytes(buf)


def simplest_field(fld: Field, payloads: list[bytes]) -> Field:
    """AUTO_FIELD when `fld` reads every one of `payloads` exactly the way
    the legacy auto decode does (s16 @ byte 0 of 2+ byte payloads, u8 of
    1-byte ones) -- the form today's firmware compiles -- else `fld`."""
    if fld.is_auto or fld.bit_len or not payloads:
        return fld
    if fld == Field(0, 2, signed=True) and all(len(p) >= 2 for p in payloads):
        return AUTO_FIELD
    if fld == Field(0, 1) and all(len(p) == 1 for p in payloads):
        return AUTO_FIELD
    return fld


def decode_raw_payload(payload: bytes) -> float:
    """The legacy auto decode -- decode_field() with no explicit field."""
    return decode_field(payload) or 0.0


# --- transforms: keep names in sync with signal_catalog.cpp's transform_* ---

def _transform_default(raw_values: list[float]) -> float:
    return raw_values[0]


def _transform_sum(raw_values: list[float]) -> float:
    return raw_values[0] + raw_values[1]


def _transform_difference(raw_values: list[float]) -> float:
    return raw_values[1] - raw_values[0]


def _transform_average(raw_values: list[float]) -> float:
    return (raw_values[0] + raw_values[1]) / 2.0


def _transform_min(raw_values: list[float]) -> float:
    return min(raw_values[0], raw_values[1])


def _transform_max(raw_values: list[float]) -> float:
    return max(raw_values[0], raw_values[1])


TRANSFORMS: dict[str, Callable[[list[float]], float]] = {
    "default": _transform_default,
    "sum": _transform_sum,
    "difference": _transform_difference,
    "average": _transform_average,
    "min": _transform_min,
    "max": _transform_max,
}

# Names usable from the monitor tool's "New Compound Signal..." builder --
# generic two-request combiners, as opposed to bespoke ones like
# actual_intake_pressure that a signal can still be hand-given in signals.json.
COMPOUND_TRANSFORMS: dict[str, str] = {
    "sum": "A + B",
    "difference": "B − A",
    "average": "Average of A and B",
    "min": "Minimum of A and B",
    "max": "Maximum of A and B",
}


# --- unit presentation: mirror of signal_catalog.cpp's unit_* helpers ---

@dataclass(frozen=True)
class UnitInfo:
    key: str  # signals.json "unit" / C++ UnitType enumerator
    description: str  # what the unit pickers list
    metric: str
    imperial: str


# In core::UnitType ORDER -- the position is the wire ordinal the phone
# configurator reads (platform/esp32/web/index.html's UNITS), so new units
# are only ever appended.
UNITS: list[UnitInfo] = [
    UnitInfo("None", "None (plain number)", "", ""),
    UnitInfo("Angle", "Angle (°)", "°", "°"),
    UnitInfo("Percent", "Percent (%)", "%", "%"),
    UnitInfo("Pressure", "Pressure (bar/psi)", "bar", "psi"),
    UnitInfo("RPM", "RPM", "rpm", "rpm"),
    UnitInfo("Speed", "Speed (kph/mph)", "kph", "mph"),
    UnitInfo("Temp", "Temp (°C/°F)", "°C", "°F"),
    UnitInfo("Torque", "Torque (Nm/lb-ft)", "Nm", "lb-ft"),
    UnitInfo("Volume", "Volume (L/gal)", "L", "gal"),
    UnitInfo("Length", "Length (mm/in)", "mm", "in"),
    UnitInfo("Voltage", "Voltage (V)", "V", "V"),
    UnitInfo("Current", "Current (A)", "A", "A"),
    UnitInfo("Illuminance", "Brightness (lx)", "lx", "lx"),
    UnitInfo("Time", "Time (min)", "min", "min"),
    UnitInfo("TempDelta", "Temp difference (Δ°C/Δ°F)", "°C", "°F"),
    UnitInfo("Enum", "Enum (value → label)", "", ""),
]
UNIT_TYPES: list[str] = [u.key for u in UNITS]
_UNIT_BY_KEY = {u.key: u for u in UNITS}

# Old unit keys still accepted on load. "Degrees" was the angle unit's name
# until several temperatures got tagged with it by mistake.
UNIT_ALIASES = {"Degrees": "Angle"}


def unit_description(unit: str) -> str:
    info = _UNIT_BY_KEY.get(unit)
    return info.description if info else unit


def unit_label(unit: str, is_metric: bool) -> str:
    info = _UNIT_BY_KEY.get(unit)
    if info is None:
        return ""
    return info.metric if is_metric else info.imperial


def unit_decimals(unit: str, is_metric: bool) -> int:
    if unit == "Pressure":
        return 2 if is_metric else 1
    if unit == "Volume":
        return 1
    if unit == "Length":
        return 0 if is_metric else 2
    if unit == "Voltage":
        return 2
    if unit == "Current":
        return 1
    return 0


def to_imperial(unit: str, metric_value: float) -> float:
    return {
        "Pressure": metric_value * 14.5038,
        "Speed": metric_value / 1.609,
        "Temp": metric_value * 1.8 + 32.0,
        "Torque": metric_value * 0.7375621493,
        "Volume": metric_value / 3.785,
        "Length": metric_value / 25.4,
        # A temperature DIFFERENCE scales like Temp but has no zero offset.
        "TempDelta": metric_value * 1.8,
    }.get(unit, metric_value)


@dataclass
class Request:
    byte1: int
    byte2: int
    offset: float
    scale: float
    ecu: Ecu | None = None  # None = not known yet (WIP signals only)
    field: Field = AUTO_FIELD

    @property
    def did(self) -> tuple[int, int]:
        return (self.byte1, self.byte2)

    def decode(self, payload: bytes) -> float | None:
        """This request's (decoded + offset) * scale, or None if `payload`
        is too short for its field."""
        v = decode_field(payload, self.field)
        return None if v is None else (v + self.offset) * self.scale

    def raw_for(self, value: float) -> int:
        """The raw field value that decodes closest to `value` -- decode()
        run backwards."""
        if self.scale == 0:
            return 0
        return round(value / self.scale - self.offset)


@dataclass
class EnumEntry:
    value: int
    label: str


@dataclass
class Point:
    """One raw -> true-value reference pair kept with a signal, for fitting
    offset/scale over many points and for looking back at later. `value` is
    the true reading (None for an Enum point, which records `label`)."""

    payload: str  # hex, the whole response payload after the DID
    value: float | None = None
    label: str = ""
    note: str = ""
    source: str = "manual"  # manual | live | review:<capture>@<t>s
    at: float = 0.0  # epoch seconds the point was recorded

    @property
    def payload_bytes(self) -> bytes:
        return bytes.fromhex(self.payload)


# Registry statuses, as tools/gen_catalog.py reads them (STATUSES there),
# with the label the tool shows for each.
STATUS_LABELS = {"active": "Active", "internal": "Internal", "wip": "WIP"}
# Compiled into the firmware: everything but WIP.
FIRMWARE_STATUSES = ("active", "internal")


@dataclass
class Signal:
    id: int
    name: str
    unit: str
    cycle_time_ms: int
    transform: str
    display_min: float
    display_max: float
    requests: list[Request] = field(default_factory=list)
    # Two-stage registry fields. "active" and "internal" signals are what
    # tools/gen_catalog.py compiles into the firmware (internal ones never
    # reach a panel); "wip" signals live in the same registry file but are
    # excluded from the compiled catalog and from the engine's known-DID
    # routing (see Catalog._reindex) -- the live monitor still treats them as
    # unknown/raw, same as before this file and wip_signals.json merged.
    status: str = "active"  # one of STATUSES
    # What the signal is, in a sentence, for the phone configurator (see
    # tools/gen_catalog.py). Notes stay the decoding scratchpad.
    description: str = ""
    notes: str = ""
    samples: list[str] = field(default_factory=list)  # hex strings, WIP reference
    updated_at: float = field(default_factory=time.time)
    enum: list[EnumEntry] = field(default_factory=list)  # unit == "Enum" only
    points: list[Point] = field(default_factory=list)

    def transform_fn(self) -> Callable[[list[float]], float]:
        return TRANSFORMS.get(self.transform, _transform_default)

    def enum_label(self, value: float) -> str:
        """The label an Enum signal shows for `value`, or "?<n>" when the
        value has no entry yet."""
        n = int(round(value))
        for entry in self.enum:
            if entry.value == n:
                return entry.label
        return f"?{n}"

    def format_value(self, value: float, is_metric: bool = True) -> str:
        """`value` (metric/native) as text with its unit, for the tool's
        tables -- the enum label for an Enum signal."""
        if self.unit == "Enum":
            return self.enum_label(value)
        shown = value if is_metric else to_imperial(self.unit, value)
        label = unit_label(self.unit, is_metric)
        text = f"{shown:.{unit_decimals(self.unit, is_metric)}f}"
        return f"{text} {label}" if label else text

    @property
    def is_wip(self) -> bool:
        return self.status == "wip"

    @property
    def in_firmware(self) -> bool:
        """Compiled into the firmware: active or internal."""
        return self.status in FIRMWARE_STATUSES

    @property
    def is_composite(self) -> bool:
        """True if this signal's value is COMPUTED from more than one request
        or a non-passthrough transform (e.g. actual intake pressure = intake
        minus ambient), rather than a single raw measurement scaled to units.
        The Signals watchlist currently shows only direct (non-composite)
        signals."""
        return len(self.requests) > 1 or self.transform != "default"


class Catalog:
    """The known-signal catalog, plus a DID index for fast lookup."""

    def __init__(self, signals: list[Signal], path: Path | None = None):
        self.signals = signals
        self.path = path
        # (byte1, byte2) -> list of (signal, request_index)
        self._did_index: dict[tuple[int, int], list[tuple[Signal, int]]] = {}
        self._reindex()

    def _reindex(self) -> None:
        """Rebuilds the DID index from COMPILED (active or internal) signals
        only. This is what
        signals_for_did()/known_dids() read, which is in turn what the engine
        (session.py) uses to decide "known, decode it" vs "unknown, log it
        raw" -- so a WIP-status signal keeps surfacing exactly like an
        undiscovered DID does, even though it now lives in this same file.
        Any-status lookup (for the Registry/WIP tabs) goes through
        find_by_did() instead, which scans self.signals directly."""
        self._did_index.clear()
        for sig in self.signals:
            if not sig.in_firmware:
                continue
            for i, req in enumerate(sig.requests):
                self._did_index.setdefault(req.did, []).append((sig, i))

    @classmethod
    def load(cls, path: Path | None = None) -> "Catalog":
        path = path or DEFAULT_SIGNALS_JSON
        data = json.loads(Path(path).read_text())
        signals: list[Signal] = []
        for s in data["signals"]:
            reqs = [
                Request(
                    byte1=_as_int(r["byte1"]),
                    byte2=_as_int(r["byte2"]),
                    offset=float(r["offset"]),
                    scale=float(r["scale"]),
                    ecu=_ecu_from_json(r.get("ecu")),
                    field=_field_from_json(r.get("field")),
                )
                for r in s["requests"]
            ]
            signals.append(
                Signal(
                    id=int(s["id"]),
                    name=s["name"],
                    unit=UNIT_ALIASES.get(s["unit"], s["unit"]),
                    cycle_time_ms=int(s["cycle_time_ms"]),
                    transform=s.get("transform", "default"),
                    display_min=float(s["display_min"]),
                    display_max=float(s["display_max"]),
                    requests=reqs,
                    status=s.get("status", "active"),
                    description=s.get("description", ""),
                    notes=s.get("notes", ""),
                    samples=list(s.get("samples", [])),
                    updated_at=float(s.get("updated_at", 0.0)),
                    enum=[
                        EnumEntry(int(e["value"]), str(e["label"]))
                        for e in s.get("enum", [])
                    ],
                    points=[_point_from_json(p) for p in s.get("points", [])],
                )
            )
        return cls(signals, path=Path(path))

    def signals_for_did(
        self, did: tuple[int, int], rx_id: int | None = None
    ) -> list[tuple[Signal, int]]:
        """Compiled-signal matches only -- see _reindex(). With `rx_id` (the
        CAN id the response came in on), only requests owned by that module
        match, so the same DID number answered by two modules isn't
        mistaken for one signal."""
        pairs = self._did_index.get(did, [])
        if rx_id is None:
            return pairs
        return [
            (sig, i) for sig, i in pairs
            if sig.requests[i].ecu is None or sig.requests[i].ecu.rx == rx_id
        ]

    def known_dids(self) -> set[tuple[int, int]]:
        """Compiled-signal DIDs only -- see _reindex()."""
        return set(self._did_index.keys())

    def find_by_did(
        self, did: tuple[int, int], rx_id: int | None = None
    ) -> list[Signal]:
        """Any-status lookup (WIP included) -- for the Learn tab's "already
        has a WIP entry" check and the editor dialog's resume/edit prefill.
        Unlike signals_for_did(), does not go through the active-only index.
        `rx_id` narrows it the same way: requests for another module don't
        match, requests with no ECU yet do."""
        return [
            sig for sig in self.signals
            if any(r.did == did and (rx_id is None or r.ecu is None
                                     or r.ecu.rx == rx_id)
                   for r in sig.requests)
        ]

    def next_id(self) -> int:
        return (max((s.id for s in self.signals), default=-1)) + 1

    # --- registry write-back ---

    def add_signal(self, signal: Signal) -> None:
        """Appends a brand-new signal. Always at the end -- see
        Catalog.update_signal's docstring for why insertion position matters
        for the compiled catalog's on-device persisted config."""
        self.signals.append(signal)
        self._reindex()

    def update_signal(self, signal: Signal) -> None:
        """Replaces an existing entry (matched by .id) in place, preserving
        its position in self.signals. Position matters: the firmware's
        Config::signals[] is a flat array persisted in flash, indexed by the
        compiled catalog's array position (resolved via signal_catalog_find,
        never a raw index) -- reordering active entries between builds would
        misalign already-saved on-device warning thresholds. Editing a
        signal's fields (including flipping active<->wip) must never change
        where it sits relative to other signals."""
        for i, existing in enumerate(self.signals):
            if existing.id == signal.id:
                self.signals[i] = signal
                self._reindex()
                return
        raise ValueError(f"no signal with id={signal.id} to update")

    def remove_signal(self, signal_id: int) -> None:
        self.signals = [s for s in self.signals if s.id != signal_id]
        self._reindex()

    def move_signal(self, signal_id: int, delta: int) -> bool:
        """Swaps a signal with its immediate neighbor `delta` positions away
        (-1 = up/earlier, +1 = down/later) in the registry's own list order
        -- the same order gen_catalog.py compiles active entries into, and
        so (see update_signal's docstring) the same order a shipped device's
        Config::signals[] would end up indexed by. No device has saved
        config yet, so reordering is safe today -- deliberately exposed as
        an explicit user action (the Registry tab's Move Up/Down) rather
        than something that happens as a side effect of anything else.
        Returns False (no-op) if signal_id is already at that end of the
        list; raises if signal_id isn't in the registry at all."""
        idx = next((i for i, s in enumerate(self.signals) if s.id == signal_id), None)
        if idx is None:
            raise ValueError(f"no signal with id={signal_id}")
        new_idx = idx + delta
        if not (0 <= new_idx < len(self.signals)):
            return False
        self.signals[idx], self.signals[new_idx] = self.signals[new_idx], self.signals[idx]
        return True

    def save(self, path: Path | None = None) -> Path:
        """Writes the catalog back to signals.json, preserving the file's
        top-level keys/order. After this, run tools/gen_catalog.py to
        regenerate the firmware's signal_catalog.generated.inc."""
        path = Path(path or self.path or DEFAULT_SIGNALS_JSON)
        # Reload to preserve _comment / unit_types and any keys we don't model.
        try:
            root = json.loads(path.read_text())
        except FileNotFoundError:
            root = {}
        root["unit_types"] = UNIT_TYPES
        root["signals"] = [signal_to_json(s) for s in self.signals]
        path.write_text(json.dumps(root, indent=2, ensure_ascii=False) + "\n")
        return path


def _as_int(v) -> int:
    if isinstance(v, str):
        return int(v, 16) if v.lower().startswith("0x") else int(v)
    return int(v)


def _ecu_from_json(e: dict | None) -> Ecu | None:
    if not e:
        return None
    return Ecu(_as_int(e["tx"]), _as_int(e["rx"]))


def _field_from_json(f: dict | None) -> Field:
    if not f:
        return AUTO_FIELD
    return Field(
        start=int(f.get("start", 0)),
        length=int(f.get("len", 0)),
        signed=bool(f.get("signed", False)),
        bit_shift=int(f.get("bit_shift", 0)),
        bit_len=int(f.get("bit_len", 0)),
        little_endian=bool(f.get("little_endian", False)),
    )


def _point_from_json(p: dict) -> Point:
    value = p.get("value")
    return Point(
        payload=str(p.get("payload", "")),
        value=None if value is None else float(value),
        label=str(p.get("label", "")),
        note=str(p.get("note", "")),
        source=str(p.get("source", "manual")),
        at=float(p.get("at", 0.0)),
    )


def _request_to_json(r: Request) -> dict:
    out: dict = {
        "byte1": f"0x{r.byte1:02X}",
        "byte2": f"0x{r.byte2:02X}",
        "offset": r.offset,
        "scale": r.scale,
    }
    # Optional keys are only written when set, so a legacy request reads
    # exactly as it always has.
    if r.ecu is not None:
        out["ecu"] = {"tx": f"0x{r.ecu.tx:03X}", "rx": f"0x{r.ecu.rx:03X}"}
    if not r.field.is_auto:
        fld: dict = {
            "start": r.field.start,
            "len": r.field.length,
            "signed": r.field.signed,
        }
        if r.field.bit_len:
            fld["bit_shift"] = r.field.bit_shift
            fld["bit_len"] = r.field.bit_len
        if r.field.little_endian:
            fld["little_endian"] = True
        out["field"] = fld
    return out


def _point_to_json(p: Point) -> dict:
    out: dict = {"payload": p.payload}
    if p.value is not None:
        out["value"] = p.value
    if p.label:
        out["label"] = p.label
    if p.note:
        out["note"] = p.note
    out["source"] = p.source
    out["at"] = p.at
    return out


def signal_to_json(s: Signal) -> dict:
    out = {
        "id": s.id,
        "name": s.name,
        "description": s.description,
        "unit": s.unit,
        "cycle_time_ms": s.cycle_time_ms,
        "transform": s.transform,
        "display_min": s.display_min,
        "display_max": s.display_max,
        "requests": [_request_to_json(r) for r in s.requests],
        "status": s.status,
        "notes": s.notes,
        "samples": s.samples,
        "updated_at": s.updated_at,
    }
    if s.enum:
        out["enum"] = [{"value": e.value, "label": e.label} for e in s.enum]
    if s.points:
        out["points"] = [_point_to_json(p) for p in s.points]
    return out
