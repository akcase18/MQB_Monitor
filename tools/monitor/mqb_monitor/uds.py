"""ISO-TP (ISO 15765-2) reassembly and UDS (ISO 14229) message parsing for
passive sniffing.

The firmware's UdsClient only ever deals in single-frame UDS (a whole
read-data-by-identifier response fits in one CAN frame), but a real bus --
especially whatever the OBDEleven pulls -- carries multi-frame responses too,
so this reassembles First/Consecutive frames into a complete service data
unit before parsing. Being a passive observer, it is deliberately lenient:
Flow-Control frames are ignored (we don't participate), and consecutive-frame
sequence numbers are not strictly enforced, since a sniffer can miss frames.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .can_frame import CanFrame

# UDS service IDs we care about (read-data-by-identifier and its response).
SID_READ_DATA_BY_ID = 0x22
SID_READ_DATA_BY_ID_RESP = 0x62
SID_NEGATIVE_RESPONSE = 0x7F

# ISO-TP PCI frame types (high nibble of byte 0).
PCI_SINGLE = 0x0
PCI_FIRST = 0x1
PCI_CONSECUTIVE = 0x2
PCI_FLOW_CONTROL = 0x3

_MAX_SDU = 4095  # ISO-TP classic 12-bit length cap; also a runaway guard.


@dataclass
class Sdu:
    """A reassembled ISO-TP service data unit."""

    can_id: int
    data: bytes
    timestamp: float
    n_frames: int  # how many CAN frames composed this SDU (1 = single-frame)
    extended: bool


@dataclass
class _Pending:
    expected_len: int
    buf: bytearray
    timestamp: float
    n_frames: int
    extended: bool


class IsoTpReassembler:
    """Feed it CanFrames; get back completed Sdus. State is per CAN id."""

    def __init__(self) -> None:
        self._pending: dict[int, _Pending] = {}

    def feed(self, frame: CanFrame) -> Sdu | None:
        if not frame.data:
            return None
        pci_type = frame.data[0] >> 4

        if pci_type == PCI_SINGLE:
            length = frame.data[0] & 0x0F
            if length == 0 or length > len(frame.data) - 1:
                return None
            self._pending.pop(frame.can_id, None)
            return Sdu(
                can_id=frame.can_id,
                data=bytes(frame.data[1 : 1 + length]),
                timestamp=frame.timestamp,
                n_frames=1,
                extended=frame.extended,
            )

        if pci_type == PCI_FIRST:
            length = ((frame.data[0] & 0x0F) << 8) | frame.data[1]
            if length == 0 or length > _MAX_SDU:
                return None
            buf = bytearray(frame.data[2:8])
            self._pending[frame.can_id] = _Pending(
                expected_len=length,
                buf=buf,
                timestamp=frame.timestamp,
                n_frames=1,
                extended=frame.extended,
            )
            return None

        if pci_type == PCI_CONSECUTIVE:
            pend = self._pending.get(frame.can_id)
            if pend is None:
                return None  # CF with no FF -- we joined mid-stream; drop
            pend.buf.extend(frame.data[1:8])
            pend.n_frames += 1
            if len(pend.buf) >= pend.expected_len:
                self._pending.pop(frame.can_id, None)
                return Sdu(
                    can_id=frame.can_id,
                    data=bytes(pend.buf[: pend.expected_len]),
                    timestamp=pend.timestamp,
                    n_frames=pend.n_frames,
                    extended=pend.extended,
                )
            return None

        # Flow control (or anything else): passive observer ignores it.
        return None


@dataclass
class UdsMessage:
    """A parsed UDS read-data-by-identifier request or response."""

    kind: str  # "request" | "response" | "negative"
    can_id: int
    did: tuple[int, int]  # the (byte1, byte2) identifier
    payload: bytes  # response data bytes after the DID (empty for requests)
    timestamp: float
    n_frames: int
    nrc: int | None = None  # negative-response code, for kind == "negative"

    @property
    def did_str(self) -> str:
        return f"{self.did[0]:02X} {self.did[1]:02X}"


def parse_uds(sdu: Sdu) -> UdsMessage | None:
    """Interprets a reassembled SDU as a read-data-by-identifier message, or
    returns None if it isn't one we track."""
    d = sdu.data
    if len(d) < 1:
        return None

    service = d[0]

    if service == SID_READ_DATA_BY_ID and len(d) >= 3:
        return UdsMessage(
            kind="request",
            can_id=sdu.can_id,
            did=(d[1], d[2]),
            payload=b"",
            timestamp=sdu.timestamp,
            n_frames=sdu.n_frames,
        )

    if service == SID_READ_DATA_BY_ID_RESP and len(d) >= 3:
        return UdsMessage(
            kind="response",
            can_id=sdu.can_id,
            did=(d[1], d[2]),
            payload=bytes(d[3:]),
            timestamp=sdu.timestamp,
            n_frames=sdu.n_frames,
        )

    if service == SID_NEGATIVE_RESPONSE and len(d) >= 3:
        # 0x7F <rejected-service> <NRC>. Only surface rejects of 0x22.
        if d[1] == SID_READ_DATA_BY_ID:
            return UdsMessage(
                kind="negative",
                can_id=sdu.can_id,
                did=(0, 0),
                payload=b"",
                timestamp=sdu.timestamp,
                n_frames=sdu.n_frames,
                nrc=d[2],
            )

    return None


NRC_NAMES = {
    0x10: "generalReject",
    0x11: "serviceNotSupported",
    0x12: "subFunctionNotSupported",
    0x13: "incorrectMessageLengthOrInvalidFormat",
    0x22: "conditionsNotCorrect",
    0x31: "requestOutOfRange",
    0x33: "securityAccessDenied",
    0x35: "invalidKey",
    0x78: "requestCorrectlyReceived-ResponsePending",
    0x7E: "subFunctionNotSupportedInActiveSession",
    0x7F: "serviceNotSupportedInActiveSession",
}
