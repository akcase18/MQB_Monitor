#!/usr/bin/env python3
"""MQB Monitor entry point.

  python3 run.py                 launch the GUI, auto-detecting the Teensy port
  python3 run.py --demo          launch the GUI on synthesized traffic (no HW)
  python3 run.py --port /dev/... launch the GUI on a specific serial port
  python3 run.py --selftest      headless: run demo traffic through the engine
                                 and print decoded signals (no GUI, no PySide6)
  python3 run.py --fit ID        headless: fit signal ID's stored points and
                                 search its payload for the field they match

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
from mqb_monitor.checks import run_checks
from mqb_monitor.session import MonitorEngine
from mqb_monitor.solver import fit_enum, fit_points, search_enum_fields, search_fields
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
    for u in snap.unknown.values():
        ecu = u.ecu.label() if u.ecu else f"?/{u.rx_id:03X}"
        print(f"  {u.did[0]:02X} {u.did[1]:02X}  {ecu}  x{u.count}  "
              f"{len(u.distinct)} distinct  last=[{u.last.hex(' ')}]")

    print("\nchecks:")
    checks_ok = run_checks()

    ok = len(decoded) > 0 and len(snap.unknown) > 0 and checks_ok
    print("\nSELFTEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def fit_report(signal_id: int, top: int = 5) -> int:
    sig = next((s for s in Catalog.load().signals if s.id == signal_id), None)
    if sig is None:
        print(f"no signal with id {signal_id}")
        return 1
    print(f"[{sig.id}] {sig.name} ({sig.unit}, {sig.status}), "
          f"{len(sig.points)} point(s)")
    if not sig.points:
        print("no points stored yet")
        return 0
    if len(sig.requests) != 1:
        print("combines several requests -- fitting covers single-request "
              "signals only")
        return 0
    fld = sig.requests[0].field
    enum = sig.unit == "Enum" or any(p.label.strip() for p in sig.points)
    if enum:
        print(f"\ncurrent field ({fld.describe()}): {fit_enum(sig.points, fld).summary()}")
        found = search_enum_fields(sig.points)
    else:
        fit = fit_points(sig.points, fld)
        print(f"\ncurrent field ({fld.describe()}): {fit.summary()}")
        if fit.ok:
            print(f"  offset {fit.offset:g}, scale {fit.scale:g}")
        found = search_fields(sig.points)
    if not found:
        print("\nfield search: not enough points to search")
        return 0
    print(f"\nbest of {len(found)} field(s):")
    for c in found[:top]:
        result = c.mapping or c.fit
        line = f"  {c.field.describe():22s} {result.summary()}"
        if c.fit:
            line += f"\n  {'':22s} offset {c.fit.offset:g}, scale {c.fit.scale:g}"
        if c.also:
            line += (f"\n  {'':22s} same result: "
                     + ", ".join(f.describe() for f in c.also[:4]))
        print(line)
    return 0


def capture_summary(path: str) -> int:
    from mqb_monitor.capture import read_capture
    from mqb_monitor.emulator import Recording, summarize

    try:
        _, frames = read_capture(Path(path))
    except (OSError, ValueError) as e:
        print(f"can't read {path}: {e}")
        return 1
    print("\n".join(summarize(Recording.from_frames(frames))))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="MQB UDS CAN monitor/decoder")
    ap.add_argument("--demo", action="store_true", help="synthesized traffic")
    ap.add_argument("--port", help="serial port of the Teensy sniffer")
    ap.add_argument("--selftest", action="store_true", help="headless engine test")
    ap.add_argument("--fit", type=int, metavar="ID",
                    help="fit a signal's stored points (headless)")
    ap.add_argument("--summary", metavar="CAPTURE",
                    help="list what a dongle did to each module in a capture "
                         "(headless)")
    ap.add_argument("--active", action="store_true",
                    help="open the bus in NORMAL mode (can transmit) instead "
                         "of listen-only -- for the active-probing phase")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if args.fit is not None:
        return fit_report(args.fit)
    if args.summary:
        return capture_summary(args.summary)

    from mqb_monitor.gui import run_gui  # lazy: needs PySide6

    return run_gui(port=args.port, demo=args.demo, listen_only=not args.active)


if __name__ == "__main__":
    sys.exit(main())
