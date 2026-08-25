#!/usr/bin/env python3
"""Pulls the dashboard firmware's black-box log over USB serial and decodes
it to CSV -- the scriptable CLI half of this tool. run.py alongside it is
the GUI for the same job; both go through mqb_logs/ (logfile.py for the
decode, device.py for the serial command set), so the two can't drift apart.

Talks a small bespoke command set (LOGDUMP / LOGSTATUS / LOGCLEAR) to
env:teensymm's own USB-CDC handler (src/main.cpp's handle_log_command(),
guarded by #ifdef ARDUINO) -- NOT the SLCAN protocol env:can_sniffer speaks
to tools/monitor over a *different* Teensy board. Requires pyserial (already
a tools/monitor dependency; `pip install pyserial` if running this script
standalone) -- and nothing else: no PySide6 on this path.

See mqb_logs/logfile.py for the on-disk format and why chunk boundaries are
found by scanning rather than read from a length field.

Usage:
    python3 tools/log_export/dump_log.py --port /dev/cu.usbmodemXXXX out.csv
    python3 tools/log_export/dump_log.py --list-ports
    python3 tools/log_export/dump_log.py --status --port /dev/cu.usbmodemXXXX
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mqb_logs.device import (  # noqa: E402  (must follow the path insert)
    DEFAULT_BAUD,
    LogDevice,
    available_ports,
    guess_teensy_port,
)
from mqb_logs.logfile import (  # noqa: E402
    format_bytes,
    load_signal_names,
    parse_chunks,
    write_csv,
)


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
        for device, desc in available_ports():
            print(f"{device}\t{desc}")
        return 0

    port = args.port or guess_teensy_port()
    if not port:
        print("No serial port found -- pass --port explicitly.", file=sys.stderr)
        return 1
    device = LogDevice(port, args.baud)

    if args.status:
        status = device.status()
        if status is None:
            print(f"{port} sent no status line -- is this the dashboard "
                  f"(env:teensymm) and not the CAN sniffer?", file=sys.stderr)
            return 1
        print(status.raw)
        return 0

    if args.clear:
        device.clear()
        print("Sent LOGCLEAR.")
        return 0

    if not args.out_csv:
        print("out_csv is required unless --status/--clear/--list-ports.", file=sys.stderr)
        return 1

    print(f"Dumping from {port} ...")
    data = device.dump()
    print(f"Received {format_bytes(len(data))}.")

    if args.raw_out:
        args.raw_out.write_bytes(data)

    chunks = parse_chunks(data)
    total = write_csv(chunks, args.out_csv, load_signal_names())
    print(f"Wrote {total} records from {len(chunks)} chunks to {args.out_csv}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
