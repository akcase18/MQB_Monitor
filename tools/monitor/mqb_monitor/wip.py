"""Work-in-progress signal notes -- a holding area for UDS DIDs you've
started reverse-engineering but haven't fully calibrated yet.

Deliberately separate from core/vehicle/data/signals.json (the canonical,
firmware-shared catalog -- see catalog.py): nothing in here is ever read by
tools/gen_catalog.py or the firmware. It's scratch space so partial progress
(a guessed name, a unit, a couple of raw samples, free-form notes, maybe an
unconfirmed offset/scale) survives between monitor sessions without any risk
of an unverified value reaching the dashboard. A WIP entry only affects the
real catalog when you explicitly promote it via "Add to Catalog" in the
Learn dialog, at which point its WIP entry is removed.
"""
from __future__ import annotations

import json
import time
from dataclasses import dataclass, field
from pathlib import Path

DEFAULT_WIP_JSON = Path(__file__).resolve().parent.parent / "wip_signals.json"

MAX_SAMPLES = 20  # mirrors session.MAX_UNKNOWN_SAMPLES


@dataclass
class WipSignal:
    did: tuple[int, int]
    name: str = ""
    unit: str = "None"
    cycle_time_ms: int = 100
    notes: str = ""
    offset: float | None = None
    scale: float | None = None
    display_min: float = 0.0
    display_max: float = 100.0
    samples: list[str] = field(default_factory=list)  # hex strings, oldest-first
    created_at: float = field(default_factory=time.time)
    updated_at: float = field(default_factory=time.time)

    @property
    def did_str(self) -> str:
        return f"{self.did[0]:02X} {self.did[1]:02X}"


class WipStore:
    """Keyed by DID -- saving the same DID again updates it in place rather
    than piling up duplicates as you refine a signal over several sessions."""

    def __init__(self, path: Path | None = None) -> None:
        self.path = Path(path or DEFAULT_WIP_JSON)
        self._by_did: dict[tuple[int, int], WipSignal] = {}
        self.load()

    def load(self) -> None:
        self._by_did.clear()
        if not self.path.exists():
            return
        data = json.loads(self.path.read_text())
        for entry in data.get("wip_signals", []):
            did = (_as_int(entry["did1"]), _as_int(entry["did2"]))
            self._by_did[did] = WipSignal(
                did=did,
                name=entry.get("name", ""),
                unit=entry.get("unit", "None"),
                cycle_time_ms=entry.get("cycle_time_ms", 100),
                notes=entry.get("notes", ""),
                offset=entry.get("offset"),
                scale=entry.get("scale"),
                display_min=entry.get("display_min", 0.0),
                display_max=entry.get("display_max", 100.0),
                samples=list(entry.get("samples", [])),
                created_at=entry.get("created_at", time.time()),
                updated_at=entry.get("updated_at", time.time()),
            )

    def save(self) -> None:
        entries = [
            {
                "did1": f"0x{w.did[0]:02X}",
                "did2": f"0x{w.did[1]:02X}",
                "name": w.name,
                "unit": w.unit,
                "cycle_time_ms": w.cycle_time_ms,
                "notes": w.notes,
                "offset": w.offset,
                "scale": w.scale,
                "display_min": w.display_min,
                "display_max": w.display_max,
                "samples": w.samples,
                "created_at": w.created_at,
                "updated_at": w.updated_at,
            }
            for w in self.all()
        ]
        self.path.write_text(json.dumps({"wip_signals": entries}, indent=2) + "\n")

    def get(self, did: tuple[int, int]) -> WipSignal | None:
        return self._by_did.get(did)

    def upsert(self, wip: WipSignal) -> None:
        wip.updated_at = time.time()
        wip.samples = wip.samples[-MAX_SAMPLES:]
        self._by_did[wip.did] = wip

    def remove(self, did: tuple[int, int]) -> None:
        self._by_did.pop(did, None)

    def all(self) -> list[WipSignal]:
        """Most-recently-updated first."""
        return sorted(
            self._by_did.values(), key=lambda w: w.updated_at, reverse=True
        )


def _as_int(v) -> int:
    if isinstance(v, str):
        return int(v, 16) if v.lower().startswith("0x") else int(v)
    return int(v)
