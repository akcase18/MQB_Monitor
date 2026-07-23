"""Loads and decodes against core/vehicle/data/signals.json -- the canonical
signal catalog shared with the firmware.

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


def _transform_actual_intake_pressure(raw_values: list[float]) -> float:
    # index 0 = ambient pressure, index 1 = intake pressure
    return raw_values[1] - raw_values[0]


TRANSFORMS: dict[str, Callable[[list[float]], float]] = {
    "default": _transform_default,
    "actual_intake_pressure": _transform_actual_intake_pressure,
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

    def transform_fn(self) -> Callable[[list[float]], float]:
        return TRANSFORMS.get(self.transform, _transform_default)

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
        self._did_index.clear()
        for sig in self.signals:
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
                )
            )
        return cls(signals, path=Path(path))

    def signals_for_did(self, did: tuple[int, int]) -> list[tuple[Signal, int]]:
        return self._did_index.get(did, [])

    def known_dids(self) -> set[tuple[int, int]]:
        return set(self._did_index.keys())

    def next_id(self) -> int:
        return (max((s.id for s in self.signals), default=-1)) + 1

    # --- learn-mode write-back ---

    def add_signal(self, signal: Signal) -> None:
        self.signals.append(signal)
        self._reindex()

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
    }
