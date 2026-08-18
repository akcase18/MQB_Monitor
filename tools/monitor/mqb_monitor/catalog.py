"""Loads and decodes against core/vehicle/data/signals.json -- the two-stage
signal registry shared with the firmware.

Every signal ever defined lives in this one file, tagged with a `status`:
"active" (compiled into the firmware by tools/gen_catalog.py, which filters
to active-only) or "wip" (still being figured out -- offset/scale may be a
guess, name may be a placeholder). Catalog mirrors that split: signals_for_did/
known_dids only ever resolve active signals (see Catalog._reindex), so the
live monitor keeps treating a WIP signal's DID as "unknown, log it raw" until
it's promoted -- find_by_did() is the any-status escape hatch for the
Registry/Learn/WIP tabs, which need to see everything.

Decoding here MIRRORS the firmware exactly (core/can/src/uds_client.cpp +
core/vehicle/src/signal_store.cpp) so the values shown in this tool match what
the dashboard would show:

  * A UDS response payload of 1 byte decodes as an UNSIGNED 8-bit value; a
    payload of >=2 bytes decodes as a SIGNED big-endian 16-bit value (first
    two bytes). This signedness is load-bearing -- e.g. timing correction goes
    negative.
  * Each request's raw value is (decoded + offset) * scale.
  * A signal's displayed value is transform(list of its requests' raw values).
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


def decode_raw_payload(payload: bytes) -> float:
    """Mirror of UdsClient::poll()'s raw decode. `payload` is the bytes AFTER
    the 0x62 service byte and the 2 DID bytes."""
    if len(payload) == 0:
        return 0.0
    if len(payload) == 1:
        return float(payload[0])  # unsigned 8-bit
    v = (payload[0] << 8) | payload[1]  # signed 16-bit big-endian
    if v >= 0x8000:
        v -= 0x10000
    return float(v)


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

_UNIT_LABEL = {
    "Degrees": ("°", "°"),
    "Percent": ("%", "%"),
    "Pressure": ("bar", "psi"),
    "RPM": ("rpm", "rpm"),
    "Speed": ("kph", "mph"),
    "Temp": ("°C", "°F"),
    "Torque": ("Nm", "lb-ft"),
    "Volume": ("L", "gal"),
    "None": ("", ""),
}


def unit_label(unit: str, is_metric: bool) -> str:
    metric, imperial = _UNIT_LABEL.get(unit, ("", ""))
    return metric if is_metric else imperial


def unit_decimals(unit: str, is_metric: bool) -> int:
    if unit == "Pressure":
        return 2 if is_metric else 1
    if unit == "Volume":
        return 1
    return 0


def to_imperial(unit: str, metric_value: float) -> float:
    return {
        "Pressure": metric_value * 14.5038,
        "Speed": metric_value / 1.609,
        "Temp": metric_value * 1.8 + 32.0,
        "Torque": metric_value * 0.7375621493,
        "Volume": metric_value / 3.785,
    }.get(unit, metric_value)


@dataclass
class Request:
    byte1: int
    byte2: int
    offset: float
    scale: float

    @property
    def did(self) -> tuple[int, int]:
        return (self.byte1, self.byte2)


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
    # Two-stage registry fields. "active" signals are what tools/gen_catalog.py
    # compiles into the firmware; "wip" signals live in the same registry file
    # but are excluded from the compiled catalog and from the engine's known-
    # DID routing (see Catalog._reindex) -- the live monitor still treats them
    # as unknown/raw, same as before this file and wip_signals.json merged.
    status: str = "active"  # "active" | "wip"
    notes: str = ""
    samples: list[str] = field(default_factory=list)  # hex strings, WIP reference
    updated_at: float = field(default_factory=time.time)

    def transform_fn(self) -> Callable[[list[float]], float]:
        return TRANSFORMS.get(self.transform, _transform_default)

    @property
    def is_wip(self) -> bool:
        return self.status == "wip"

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
        """Rebuilds the DID index from ACTIVE signals only. This is what
        signals_for_did()/known_dids() read, which is in turn what the engine
        (session.py) uses to decide "known, decode it" vs "unknown, log it
        raw" -- so a WIP-status signal keeps surfacing exactly like an
        undiscovered DID does, even though it now lives in this same file.
        Any-status lookup (for the Registry/WIP tabs) goes through
        find_by_did() instead, which scans self.signals directly."""
        self._did_index.clear()
        for sig in self.signals:
            if sig.status != "active":
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
                )
                for r in s["requests"]
            ]
            signals.append(
                Signal(
                    id=int(s["id"]),
                    name=s["name"],
                    unit=s["unit"],
                    cycle_time_ms=int(s["cycle_time_ms"]),
                    transform=s.get("transform", "default"),
                    display_min=float(s["display_min"]),
                    display_max=float(s["display_max"]),
                    requests=reqs,
                    status=s.get("status", "active"),
                    notes=s.get("notes", ""),
                    samples=list(s.get("samples", [])),
                    updated_at=float(s.get("updated_at", 0.0)),
                )
            )
        return cls(signals, path=Path(path))

    def signals_for_did(self, did: tuple[int, int]) -> list[tuple[Signal, int]]:
        """Active-signal matches only -- see _reindex()."""
        return self._did_index.get(did, [])

    def known_dids(self) -> set[tuple[int, int]]:
        """Active-signal DIDs only -- see _reindex()."""
        return set(self._did_index.keys())

    def find_by_did(self, did: tuple[int, int]) -> list[Signal]:
        """Any-status lookup (active or WIP) -- for the Learn tab's "already
        has a WIP entry" check and the editor dialog's resume/edit prefill.
        Unlike signals_for_did(), does not go through the active-only index."""
        return [sig for sig in self.signals if any(r.did == did for r in sig.requests)]

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
        root["signals"] = [_signal_to_json(s) for s in self.signals]
        path.write_text(json.dumps(root, indent=2) + "\n")
        return path


def _as_int(v) -> int:
    if isinstance(v, str):
        return int(v, 16) if v.lower().startswith("0x") else int(v)
    return int(v)


def _signal_to_json(s: Signal) -> dict:
    return {
        "id": s.id,
        "name": s.name,
        "unit": s.unit,
        "cycle_time_ms": s.cycle_time_ms,
        "transform": s.transform,
        "display_min": s.display_min,
        "display_max": s.display_max,
        "requests": [
            {
                "byte1": f"0x{r.byte1:02X}",
                "byte2": f"0x{r.byte2:02X}",
                "offset": r.offset,
                "scale": r.scale,
            }
            for r in s.requests
        ],
        "status": s.status,
        "notes": s.notes,
        "samples": s.samples,
        "updated_at": s.updated_at,
    }
