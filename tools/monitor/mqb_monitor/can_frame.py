"""The one raw-CAN-frame type passed around the app."""
from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class CanFrame:
    """A single received CAN frame.

    `timestamp` is host-monotonic seconds (time.monotonic()) captured when the
    frame arrived over USB -- the Teensy's own SLCAN millisecond timestamp is
    coarse (16-bit, wraps every 60 s) and off by USB latency, so for
    request/response pairing we prefer the host clock. `direction` marks frames
    the tool itself transmitted (active-probe phase) vs. observed on the bus.
    """

    timestamp: float
    can_id: int
    data: bytes
    extended: bool = False
    direction: str = "rx"  # "rx" observed, "tx" sent by this tool

    def hex_data(self) -> str:
        return " ".join(f"{b:02X}" for b in self.data)

    def id_str(self) -> str:
        return f"{self.can_id:08X}" if self.extended else f"{self.can_id:03X}"
