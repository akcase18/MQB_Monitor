#!/usr/bin/env python3
"""Throughput characterization -- see THROUGHPUT_TEST_PLAN.md and README.md.

  run.py dash  --v4 PORT --v5 PORT ...   Phase 1: v4 fakes the car for v5
  run.py car c0|c1|c2|c3|c4 --v4 PORT    Phase 2: v4 fakes the dash for the car
  run.py car run --v4 PORT --job ...     Phase 2, any job list (C4b, C5, C6)
  run.py listen --v4 PORT                Phase 3: v4 only listens
  run.py analyze DIR                     re-analyze a saved run
  run.py selftest                        check the analysis on made-up data
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from mqb_bench import analyze  # noqa: E402
from mqb_bench.catalog import Module, find_module, load_modules  # noqa: E402
from mqb_bench.link import BenchLink, DashLink, LinkError  # noqa: E402

RESULTS = HERE / "results"
ENGINE_TX = 0x7E0


def new_run_dir(label: str) -> Path:
    stamp = datetime.now().strftime("%Y-%m-%d_%H%M%S")
    safe = "".join(c if c.isalnum() or c in "-_." else "_" for c in label)
    path = RESULTS / f"{stamp}_{safe}"
    path.mkdir(parents=True)
    return path


def module_map(modules: list[Module]) -> dict[str, str]:
    return {f"{m.tx:03X}": f"{m.rx:03X}" for m in modules}


def configure_modules(v4: BenchLink, modules: list[Module], lengths: bool) -> None:
    v4.command("BH")
    v4.command("BC")
    for m in modules:
        v4.command(f"BM {m.tx:X} {m.rx:X}")
        if lengths:
            for did, length in m.dids.items():
                v4.command(f"BD {m.tx:X} {did:X} {length}")


def send_latencies(v4: BenchLink, latencies: list[int]) -> None:
    """BL lines short enough for the dash's 39-character command limit."""
    line = "BL"
    for us in latencies:
        if len(line) + 1 + len(str(us)) > 39:
            v4.command(line)
            line = "BL"
        line += f" {us}"
    if line != "BL":
        v4.command(line)


def capture(v4: BenchLink, run_dir: Path, seconds: float, start: str = "") -> int:
    """Records `seconds` of frames, sending the bench command `start` once
    recording is under way."""
    v4.start_capture(run_dir / "frames.csv")
    if start:
        v4.command(start)
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        left = end - time.monotonic()
        print(f"\r  capturing... {left:4.0f} s left, {v4.frames_captured} frames", end="", flush=True)
        time.sleep(min(1.0, max(left, 0)))
    print()
    return v4.stop_capture()


def finish(run_dir: Path, run: dict, counters: dict | None) -> dict:
    run["v4_counters"] = counters or {}
    (run_dir / "run.json").write_text(json.dumps(run, indent=2))
    summary, text = analyze.analyze_dir(run_dir)
    print(text)
    print(f"Saved to {run_dir}")
    return summary


def parse_int_list(text: str, base: int = 10) -> list[int]:
    return [int(x, base) for x in text.split(",") if x.strip()]


# -- Phase 1 ------------------------------------------------------------------


def cmd_dash(args: argparse.Namespace) -> None:
    modules = load_modules()
    latencies = parse_int_list(args.latency)
    forced = parse_int_list(args.forced_len) or [0]

    v4 = BenchLink(args.v4)
    v5 = DashLink(args.v5)
    try:
        v4.enter()
        for latency in latencies:
            for length in forced:
                label = f"{args.label}_lat{latency}us" + (f"_len{length}" if length else "")
                print(f"\n== {label}")
                configure_modules(v4, modules, lengths=True)
                if latency:
                    send_latencies(v4, [latency])
                if length:
                    v4.command(f"BFL {length}")
                if args.pending_every:
                    v4.command(f"BPEND {args.pending_every}")
                if args.drop_every:
                    v4.command(f"BDROP {args.drop_every}")
                v4.command("BS R")

                v5.set_flood(args.flood)
                print(f"  settling {args.settle:.0f} s")
                time.sleep(args.settle)

                run_dir = new_run_dir(label)
                v5.reset()
                capture(v4, run_dir, args.duration)
                poll, entries, poll_lines = v5.poll_stats()
                loop, loop_line = v5.loop_stats()
                counters = v4.counters()
                v4.command("BH")

                (run_dir / "dash.json").write_text(
                    json.dumps({"poll": poll, "entries": entries, "loop": loop}, indent=2)
                )
                (run_dir / "dash.txt").write_text("\n".join(poll_lines + [loop_line]) + "\n")
                finish(
                    run_dir,
                    {
                        "kind": "dash",
                        "label": label,
                        "v4_role": "responder",
                        "modules": module_map(modules),
                        "latencies_us": [latency],
                        "forced_len": length,
                        "flood": args.flood,
                        "pending_every": args.pending_every,
                        "drop_every": args.drop_every,
                        "notes": args.notes,
                    },
                    counters,
                )
        if args.flood:
            v5.set_flood(False)
    finally:
        v4.leave()
        v4.close()
        v5.close()


# -- Phase 2 ------------------------------------------------------------------


def tester_run(
    v4: BenchLink,
    modules: list[Module],
    jobs: list[tuple[int, int, list[int]]],
    label: str,
    args: argparse.Namespace,
    seconds: float,
    did_table: bool = False,
) -> dict:
    """One closed-loop tester run. `jobs` are (lane, module tx, DIDs)."""
    print(f"\n== {label}")
    configure_modules(v4, modules, lengths=False)
    v4.command(f"BT {args.timeout_ms}")
    v4.command(f"BFC {args.fc_bs} {args.fc_stmin:X}")
    if args.gap_us:
        v4.command(f"BGAP {args.gap_us}")
    for lane, tx, dids in jobs:
        v4.command(f"BJ {lane} {tx:X} " + " ".join(f"{d:X}" for d in dids))

    run_dir = new_run_dir(label)
    capture(v4, run_dir, seconds, start="BS T")
    counters = v4.counters()
    v4.command("BH")
    return finish(
        run_dir,
        {
            "kind": "car",
            "label": label,
            "v4_role": "requester",
            "modules": module_map(modules),
            "jobs": [f"{lane}:{tx:03X}:" + ",".join(f"{d:04X}" for d in dids) for lane, tx, dids in jobs],
            "fc": f"{args.fc_bs} {args.fc_stmin:02X}",
            "timeout_ms": args.timeout_ms,
            "gap_us": args.gap_us,
            "did_table": did_table,
            "notes": args.notes,
        },
        counters,
    )


def first_did(m: Module) -> int:
    return next(iter(m.dids))


def raw_exchange(v4: BenchLink, m: Module, payload: bytes, wait_s: float = 0.3) -> bytes | None:
    """Sends one single-frame request and returns the module's answer (its
    first single frame that isn't a 0x78, or a first frame), if any."""
    frame = bytes([len(payload)]) + payload
    frame += b"\xAA" * (8 - len(frame))
    path = RESULTS / ".c0_probe.csv"
    v4.start_capture(path)
    v4.command(f"BR {m.tx:X} {frame.hex().upper()}")
    time.sleep(wait_s)
    v4.stop_capture()
    answer = None
    for line in path.read_text().splitlines()[1:]:
        _, direction, ident, data = line.split(",")
        if direction == "R" and int(ident, 16) == m.rx:
            raw = bytes.fromhex(data)
            pci = raw[0] >> 4
            if pci == 0 and not (raw[1] == 0x7F and raw[3] == 0x78):
                answer = raw[1 : 1 + (raw[0] & 0x0F)]
                break
            if pci == 1:
                answer = raw[2:8]
                break
    path.unlink(missing_ok=True)
    return answer


def describe_answer(answer: bytes | None) -> str:
    if answer is None:
        return "no answer"
    if answer[0] == 0x7F:
        return f"NRC {answer[2]:02X}"
    return "OK"


def cmd_car_c0(v4: BenchLink, modules: list[Module], args: argparse.Namespace) -> None:
    """Does a plain 22 read work in the default session, module by module?
    If not, does it after 10 03? Puts each module back with 10 01."""
    configure_modules(v4, modules, lengths=False)
    v4.command("BS T")
    lines = [
        "# C0: plain reads by module",
        "",
        "| Module | DID | Default session | After 10 03 |",
        "|---|---|---|---|",
    ]
    for m in modules:
        did = first_did(m)
        read = bytes([0x22, did >> 8, did & 0xFF])
        plain = raw_exchange(v4, m, read)
        extended = "-"
        if plain is None or plain[0] != 0x62:
            session = raw_exchange(v4, m, bytes([0x10, 0x03]))
            if session is not None and session[0] == 0x50:
                extended = describe_answer(raw_exchange(v4, m, read))
                raw_exchange(v4, m, bytes([0x10, 0x01]))
            else:
                extended = f"10 03 refused ({describe_answer(session)})"
        lines.append(f"| {m.label} | {did:04X} | {describe_answer(plain)} | {extended} |")
        print(lines[-1])
    v4.command("BH")
    run_dir = new_run_dir("c0")
    (run_dir / "summary.md").write_text("\n".join(lines) + "\n")
    print(f"Saved to {run_dir}")


def modules_to_test(modules: list[Module], args: argparse.Namespace) -> list[Module]:
    if args.module:
        return [find_module(modules, int(t, 16)) for t in args.module.split(",")]
    return modules


def cmd_car(args: argparse.Namespace) -> None:
    modules = load_modules()
    v4 = BenchLink(args.v4)
    tag = f"_{args.label}" if args.label else ""
    try:
        v4.enter()
        if args.test == "c0":
            cmd_car_c0(v4, modules, args)

        elif args.test == "c1":
            for m in modules_to_test(modules, args):
                tester_run(v4, modules, [(0, m.tx, [first_did(m)])],
                           f"c1_{m.tx:03X}{tag}", args, args.duration or 30)

        elif args.test == "c2":
            for m in modules_to_test(modules, args):
                jobs = [(0, m.tx, [d]) for d in m.dids]
                seconds = args.duration or max(5.0, len(jobs) * 20 * 0.03)
                tester_run(v4, modules, jobs, f"c2_{m.tx:03X}{tag}", args, seconds, did_table=True)

        elif args.test == "c3":
            m = find_module(modules, int(args.module or f"{ENGINE_TX:X}", 16))
            dids = list(m.dids)[:6]
            if len(dids) < 6:
                print(f"Only {len(dids)} DIDs compiled for {m.label}; N stops there.")
            for n in parse_int_list(args.n or "1,2,3,4,5,6"):
                if n > len(dids):
                    break
                tester_run(v4, modules, [(0, m.tx, dids[:n])],
                           f"c3_{m.tx:03X}_n{n}{tag}", args, args.duration or 60)

        elif args.test == "c4":
            chosen = modules_to_test(modules, args)
            for k in range(1, len(chosen) + 1):
                jobs = [(lane, m.tx, [first_did(m)]) for lane, m in enumerate(chosen[:k])]
                tester_run(v4, modules, jobs, f"c4_{k}modules{tag}", args, args.duration or 30)

        elif args.test == "run":
            jobs = []
            for spec in args.job:
                lane, tx, dids = spec.split(":")
                jobs.append((int(lane), int(tx, 16), parse_int_list(dids, 16)))
            if not jobs:
                raise SystemExit("run needs at least one --job LANE:TX:DID[,DID...]")
            tester_run(v4, modules, jobs, args.label or "run", args, args.duration or 30)
    finally:
        v4.leave()
        v4.close()


# -- Phase 3 ------------------------------------------------------------------


def cmd_listen(args: argparse.Namespace) -> None:
    modules = load_modules()
    v4 = BenchLink(args.v4)
    try:
        v4.enter()
        configure_modules(v4, modules, lengths=False)
        run_dir = new_run_dir(args.label or "listen")
        capture(v4, run_dir, args.duration, start="BS L")
        counters = v4.counters()
        v4.command("BH")
        finish(
            run_dir,
            {"kind": "listen", "label": args.label or "listen", "v4_role": "listener",
             "modules": module_map(modules), "notes": args.notes},
            counters,
        )
    finally:
        v4.leave()
        v4.close()


# -- CLI ----------------------------------------------------------------------


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    def common(sp: argparse.ArgumentParser) -> None:
        sp.add_argument("--v4", required=True, help="the v4 dash's serial port")
        sp.add_argument("--label", default="", help="name for the results folder")
        sp.add_argument("--notes", default="", help="free text saved with the run")

    d = sub.add_parser("dash", help="Phase 1: v4 answers as the car, v5 is measured")
    common(d)
    d.add_argument("--v5", required=True, help="the v5 dash's serial port")
    d.add_argument("--duration", type=float, default=60)
    d.add_argument("--settle", type=float, default=10)
    d.add_argument("--latency", default="0", help="answer latency in µs; a comma list runs each")
    d.add_argument("--forced-len", default="", help="force every DID's data length; a comma list runs each")
    d.add_argument("--pending-every", type=int, default=0)
    d.add_argument("--drop-every", type=int, default=0)
    d.add_argument("--no-flood", dest="flood", action="store_false",
                   help="the dash's own request periods instead of back to back")
    d.set_defaults(func=cmd_dash)

    c = sub.add_parser("car", help="Phase 2: v4 asks, the car answers")
    c.add_argument("test", choices=["c0", "c1", "c2", "c3", "c4", "run"])
    common(c)
    c.add_argument("--module", default="", help="request id(s), hex, comma-separated")
    c.add_argument("--n", default="", help="c3: DIDs per request to try, e.g. 1,2,3")
    c.add_argument("--job", action="append", default=[], help="run: LANE:TX:DID[,DID...] (hex)")
    c.add_argument("--duration", type=float, default=0, help="seconds per run (default per test)")
    c.add_argument("--timeout-ms", type=int, default=100)
    c.add_argument("--fc-bs", type=int, default=0, help="block size v4 asks for in long answers")
    c.add_argument("--fc-stmin", type=lambda x: int(x, 16), default=0, help="STmin byte, hex")
    c.add_argument("--gap-us", type=int, default=0)
    c.set_defaults(func=cmd_car)

    li = sub.add_parser("listen", help="Phase 3: v4 listens only")
    common(li)
    li.add_argument("--duration", type=float, default=60)
    li.set_defaults(func=cmd_listen)

    a = sub.add_parser("analyze", help="re-analyze a saved run")
    a.add_argument("dir", type=Path)
    a.set_defaults(func=lambda args: print(analyze.analyze_dir(args.dir)[1]))

    s = sub.add_parser("selftest", help="check the analysis on made-up data")
    s.set_defaults(func=lambda args: analyze.selftest())

    args = p.parse_args()
    RESULTS.mkdir(exist_ok=True)
    try:
        args.func(args)
    except LinkError as exc:
        raise SystemExit(f"error: {exc}") from exc


if __name__ == "__main__":
    main()
