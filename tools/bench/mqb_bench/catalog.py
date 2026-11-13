"""What the bench needs from the signal registry: every module the firmware
compiles requests for, the DIDs each one is asked, and how many data bytes
each DID answers with -- so the v4 fake ECU's answers are long enough for
the dash to decode, and the car tests read real DIDs.

Reuses tools/monitor's loader rather than parsing signals.json again.
"""

from __future__ import annotations

import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools" / "monitor"))

from mqb_monitor.catalog import Catalog  # noqa: E402

# The legacy auto decode reads a u8 from a 1-byte answer or an s16 from
# bytes 0-1, so 2 bytes satisfies it either way.
AUTO_LENGTH = 2


@dataclass
class Module:
    tx: int
    rx: int
    # DID -> data bytes it answers with, in registry order.
    dids: dict[int, int] = field(default_factory=dict)

    @property
    def label(self) -> str:
        return f"{self.tx:03X}/{self.rx:03X}"


def load_modules(signals_json: Path | None = None) -> list[Module]:
    """Every module with a compiled (active or internal) request, engine
    first, each with its DIDs and their answer lengths."""
    catalog = Catalog.load(signals_json)
    modules: dict[int, Module] = {}

    for sig in catalog.signals:
        if not sig.in_firmware:
            continue
        # Stored payloads say how long the real answer is, but only a
        # single-request signal's can be pinned to one DID.
        observed: list[int] = []
        if len(sig.requests) == 1:
            observed = [len(bytes.fromhex(s)) for s in sig.samples if s]
            observed += [len(p.payload_bytes) for p in sig.points if p.payload]
        for req in sig.requests:
            if req.ecu is None:
                continue
            mod = modules.setdefault(req.ecu.tx, Module(req.ecu.tx, req.ecu.rx))
            did = (req.byte1 << 8) | req.byte2
            need = AUTO_LENGTH if req.field.is_auto else req.field.start + req.field.length
            length = max([need] + observed)
            mod.dids[did] = max(mod.dids.get(did, 0), length)

    return sorted(modules.values(), key=lambda m: (m.tx != 0x7E0, m.tx))


def find_module(modules: list[Module], tx: int) -> Module:
    for mod in modules:
        if mod.tx == tx:
            return mod
    raise SystemExit(f"No compiled signal reads module {tx:03X}")
