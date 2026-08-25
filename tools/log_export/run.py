#!/usr/bin/env python3
"""MQB Log Export entry point -- the GUI for pulling the dashboard's
black-box log off the device.

  python3 run.py                  launch the GUI, auto-detecting the port
  python3 run.py --port /dev/...  launch it on a specific serial port
  python3 run.py --folder DIR     launch it reading a storage directory
                                  (the simulator's sim_storage/, say)
  python3 run.py --check DIR      headless: decode DIR and print a session
                                  summary (no GUI, no PySide6)

dump_log.py alongside this is the scriptable CLI for the same job -- both
decode through mqb_logs/logfile.py, so they can't drift apart.

The GUI (and therefore PySide6) is imported lazily so --check runs with
nothing installed at all.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Allow running as `python3 run.py` from anywhere.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from mqb_logs import logfile


def check(directory: Path) -> int:
    """Headless decode of a storage directory -- the equivalent of the
    monitor's --selftest: proves the decode path end to end against
    sim_storage/ with no hardware and no Qt."""
    if not directory.is_dir():
        print(f"Not a directory: {directory}", file=sys.stderr)
        return 1

    data = logfile.read_storage_dir(directory)
    chunks = logfile.parse_chunks(data)
    sessions = logfile.group_sessions(chunks)
    names = logfile.load_signal_names()
    captures = logfile.read_index(directory)

    print(f"{directory}: {logfile.format_bytes(len(data))}, "
          f"{len(chunks)} chunks, {len(sessions)} sessions, "
          f"{len(captures)} captures in the index")
    for session in sessions:
        first_seq, last_seq = session.chunk_seq_range
        signals = ", ".join(
            sorted(names.get(sid, f"#{sid}") for sid in session.signal_ids)
        )
        print(f"  session {session.session_id:3d}  chunks {first_seq}-{last_seq}  "
              f"{session.record_count:7,} records  "
              f"{logfile.format_duration(session.duration_ms):>10}  {signals}")

    ok = bool(chunks)
    print("\nCHECK", "PASS" if ok else "FAIL (nothing decoded)")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="MQB black-box log offload")
    ap.add_argument("--port", help="serial port of the dashboard (env:teensymm)")
    ap.add_argument("--folder", type=Path,
                    help="read a storage directory instead of a device")
    ap.add_argument("--check", type=Path, metavar="DIR",
                    help="headless decode of DIR, then exit")
    args = ap.parse_args()

    if args.check:
        return check(args.check)

    from mqb_logs.gui import run_gui  # lazy: needs PySide6

    return run_gui(port=args.port, folder=args.folder)


if __name__ == "__main__":
    sys.exit(main())
