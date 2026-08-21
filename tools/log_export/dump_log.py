#!/usr/bin/env python3
"""Pulls the dashboard firmware's black-box log over USB serial and decodes
it to CSV.

Talks a small bespoke command set (LOGDUMP / LOGSTATUS / LOGCLEAR) to
env:teensymm's own USB-CDC handler (src/main.cpp's handle_log_command(),
guarded by #ifdef ARDUINO) -- NOT the SLCAN protocol env:can_sniffer speaks
to tools/monitor over a *different* Teensy board. Requires pyserial (already
a tools/monitor dependency; `pip install pyserial` if running this script
standalone).

On-disk format (see core/logging/inc/log_record.hpp): the device streams
each of its CHUNK_COUNT rotating chunk files back-to-back, array-index
order, skipping ones that were never written. Each chunk is a 13-byte
ChunkHeader (magic/version/session_id/chunk_seq) followed by zero or more
10-byte LogRecords, with no length prefix -- the firmware doesn't know a
chunk's exact record count without re-deriving it (IStorage has no seek to
patch a header after the fact once records span many flushes), so this
script finds each chunk's boundary the same way: scan forward for the next
occurrence of a valid header (magic + a matching version byte), which also
naturally happens at EOF for the final chunk in the stream. True
chronological order comes from each header's own chunk_seq/session_id, not
from stream order (rotation means an earlier stream position can hold a
*newer* chunk_seq than a later one, once the ring has wrapped).

Usage:
    python3 tools/log_export/dump_log.py --port /dev/cu.usbmodemXXXX out.csv
    python3 tools/log_export/dump_log.py --list-ports
    python3 tools/log_export/dump_log.py --status --port /dev/cu.usbmodemXXXX
"""
from __future__ import annotations

import argparse
import csv
import json
import struct
import sys
import time
from pathlib import Path

import serial
from serial.tools import list_ports

CHUNK_HEADER_FMT = "<IBII"  # magic, version, session_id, chunk_seq
CHUNK_HEADER_LEN = struct.calcsize(CHUNK_HEADER_FMT)
LOG_RECORD_FMT = "<IHf"  # timestamp_ms, signal_id, value
LOG_RECORD_LEN = struct.calcsize(LOG_RECORD_FMT)

LOG_MAGIC = 0x4C4F4731  # ASCII "LOG1" -- keep in sync with log_record.hpp
LOG_VERSION = 1

DEFAULT_BAUD = 115200  # irrelevant for Teensy USB-CDC, kept for pyserial's API
IDLE_TIMEOUT_S = 2.0  # LOGDUMP has no explicit "done" marker -- treat this
# much silence after the last byte as end-of-stream.

SIGNALS_JSON = Path(__file__).resolve().parents[2] / "core/vehicle/data/signals.json"


def guess_teensy_port() -> str | None:
    for p in list_ports.comports():
        text = f"{p.description} {p.manufacturer} {p.product}".lower()
        if "teensy" in text or "usb serial" in text or "usbmodem" in text:
            return p.device
    ports = list_ports.comports()
    return ports[0].device if ports else None


def load_signal_names() -> dict[int, str]:
    """id -> name, for a friendlier CSV. Best-effort -- an empty dict just
    means the CSV falls back to raw ids."""
    try:
        data = json.loads(SIGNALS_JSON.read_text())
        return {s["id"]: s["name"] for s in data["signals"]}
    except (OSError, KeyError, json.JSONDecodeError):
        return {}


def send_command(ser: serial.Serial, command: str) -> None:
    ser.write((command + "\r\n").encode("ascii"))
    ser.flush()


def read_until_idle(ser: serial.Serial, idle_timeout_s: float) -> bytes:
    chunks: list[bytes] = []
    last_data = time.monotonic()
    while time.monotonic() - last_data < idle_timeout_s:
        data = ser.read(4096)
        if data:
            chunks.append(data)
            last_data = time.monotonic()
    return b"".join(chunks)


def find_next_header(data: bytes, start: int) -> int:
    """@returns the offset of the next byte sequence at/after `start` that
    parses as a valid ChunkHeader (magic + version match), or len(data) if
    none exists."""
    pos = start
    while True:
        pos = data.find(struct.pack("<I", LOG_MAGIC), pos)
        if pos == -1 or pos + CHUNK_HEADER_LEN > len(data):
            return len(data)
        _magic, version, _session_id, _chunk_seq = struct.unpack_from(
            CHUNK_HEADER_FMT, data, pos
        )
        if version == LOG_VERSION:
            return pos
        pos += 1  # false match (magic bytes appeared inside record data)


def parse_chunks(data: bytes) -> list[tuple[int, int, list[tuple[int, int, float]]]]:
    """@returns a list of (session_id, chunk_seq, records), records being
    (timestamp_ms, signal_id, value) tuples -- NOT yet sorted into
    chronological order (see the file doc comment)."""
    chunks: list[tuple[int, int, list[tuple[int, int, float]]]] = []
    pos = 0
    while pos < len(data):
        header_end = find_next_header(data, pos)
        if header_end != pos:
            # Garbage before the next real header (shouldn't happen with a
            # well-formed dump) -- skip it rather than misparsing it.
            pos = header_end
            continue
        if pos + CHUNK_HEADER_LEN > len(data):
            break

        _magic, _version, session_id, chunk_seq = struct.unpack_from(
            CHUNK_HEADER_FMT, data, pos
        )
        body_start = pos + CHUNK_HEADER_LEN
        body_end = find_next_header(data, body_start)
        body = data[body_start:body_end]

        record_count = len(body) // LOG_RECORD_LEN  # drops a torn trailing
        # partial record, if the dump was interrupted mid-write
        records = [
            struct.unpack_from(LOG_RECORD_FMT, body, i * LOG_RECORD_LEN)
            for i in range(record_count)
        ]
        chunks.append((session_id, chunk_seq, records))

        pos = body_end
    return chunks


def write_csv(
    chunks: list[tuple[int, int, list[tuple[int, int, float]]]],
    out_path: Path,
    names: dict[int, str],
) -> int:
    # True chronological order: sort chunks by chunk_seq (ever-increasing
    # across the device's lifetime), then records within a chunk are
    # already in the order they were sampled.
    chunks_sorted = sorted(chunks, key=lambda c: c[1])

    total = 0
    with out_path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(
            ["session_id", "chunk_seq", "timestamp_ms", "signal_id", "signal_name", "value"]
        )
        for session_id, chunk_seq, records in chunks_sorted:
            for timestamp_ms, signal_id, value in records:
                writer.writerow(
                    [
                        session_id,
                        chunk_seq,
                        timestamp_ms,
                        signal_id,
                        names.get(signal_id, ""),
                        value,
                    ]
                )
                total += 1
    return total


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out_csv", nargs="?", type=Path, help="Output CSV path")
    parser.add_argument("--port", help="Serial port (default: auto-detect)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument(
        "--list-ports", action="store_true", help="List serial ports and exit"
    )
    parser.add_argument(
        "--status", action="store_true", help="Print LOGSTATUS and exit (no dump)"
    )
    parser.add_argument(
        "--clear",
        action="store_true",
        help="Send LOGCLEAR (format the log store) instead of dumping",
    )
    parser.add_argument(
        "--raw-out", type=Path, help="Also save the raw dumped bytes here"
    )
    args = parser.parse_args()

    if args.list_ports:
        for device, desc in [(p.device, p.description) for p in list_ports.comports()]:
            print(f"{device}\t{desc}")
        return 0

    port = args.port or guess_teensy_port()
    if not port:
        print("No serial port found -- pass --port explicitly.", file=sys.stderr)
        return 1

    with serial.Serial(port, args.baud, timeout=0.2) as ser:
        time.sleep(0.3)  # let USB-CDC settle before the first command
        ser.reset_input_buffer()

        if args.status:
            send_command(ser, "LOGSTATUS")
            print(read_until_idle(ser, IDLE_TIMEOUT_S).decode("ascii", "replace"))
            return 0

        if args.clear:
            send_command(ser, "LOGCLEAR")
            print("Sent LOGCLEAR.")
            return 0

        if not args.out_csv:
            print("out_csv is required unless --status/--clear/--list-ports.", file=sys.stderr)
            return 1

        print(f"Dumping from {port} ...")
        send_command(ser, "LOGDUMP")
        data = read_until_idle(ser, IDLE_TIMEOUT_S)
        print(f"Received {len(data)} bytes.")

        if args.raw_out:
            args.raw_out.write_bytes(data)

        chunks = parse_chunks(data)
        names = load_signal_names()
        total = write_csv(chunks, args.out_csv, names)
        print(f"Wrote {total} records from {len(chunks)} chunks to {args.out_csv}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
