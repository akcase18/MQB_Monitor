#!/usr/bin/env python3
"""MQB Monitor entry point.

  python3 run.py                 launch the GUI, auto-detecting the Teensy port
  python3 run.py --demo          launch the GUI on synthesized traffic (no HW)
  python3 run.py --port /dev/... launch the GUI on a specific serial port
  python3 run.py --selftest      headless: run demo traffic through the engine
                                 and print decoded signals (no GUI, no PySide6)

The GUI (and therefore PySide6) is imported lazily so --selftest runs with
nothing but pyserial installed.
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

# Allow running as `python3 run.py` from anywhere.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from mqb_monitor.catalog import Catalog
from mqb_monitor.session import MonitorEngine
from mqb_monitor.sources import DemoSource


def selftest(seconds: float = 2.0) -> int:
    catalog = Catalog.load()
    engine = MonitorEngine(catalog)
    source = DemoSource(catalog, engine.feed, period_s=0.005)
    source.start()
    time.sleep(seconds)
    source.stop()

    snap = engine.snapshot()
    print(f"frames seen: {snap.total_frames}")
    print(f"messages parsed: {len(snap.messages)}")

    decoded = [r for r in snap.readings.values() if r.value is not None]
    print(f"\ndecoded {len(decoded)}/{len(snap.readings)} known signals:")
    for r in sorted(decoded, key=lambda r: r.signal_id):
        print(f"  [{r.signal_id:2d}] {r.name:20s} = {r.value:10.3f}  "
              f"({r.updates} updates, {r.latency_ms or 0:.1f} ms latency)")

    print(f"\nunknown DIDs discovered: {len(snap.unknown)}")
    for did, u in snap.unknown.items():
        print(f"  {did[0]:02X} {did[1]:02X}  x{u.count}  "
              f"last=[{u.samples[-1].hex(' ') if u.samples else ''}]")

    ok = len(decoded) > 0 and len(snap.unknown) > 0
    print("\nSELFTEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="MQB UDS CAN monitor/decoder")
    ap.add_argument("--demo", action="store_true", help="synthesized traffic")
    ap.add_argument("--port", help="serial port of the Teensy sniffer")
    ap.add_argument("--selftest", action="store_true", help="headless engine test")
    ap.add_argument("--active", action="store_true",
                    help="open the bus in NORMAL mode (can transmit) instead "
                         "of listen-only -- for the active-probing phase")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    from mqb_monitor.gui import run_gui  # lazy: needs PySide6

    return run_gui(port=args.port, demo=args.demo, listen_only=not args.active)


if __name__ == "__main__":
    sys.exit(main())
