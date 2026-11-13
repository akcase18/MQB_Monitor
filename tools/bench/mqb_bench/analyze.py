"""Turns a bench run's frame log into exchanges and the numbers
THROUGHPUT_TEST_PLAN.md asks for.

Every UDS exchange on a module is rebuilt from the wire: the request
(single frame, or first + consecutive frames), the answer (the same, plus
any 0x78s before it), and the flow control in between. Each cycle then
splits into the two halves the plan is built on:

  responder turnaround  last request frame on the wire -> first answer frame
  requester turnaround  last answer frame on the wire  -> next request frame

Times are corrected to the wire. The v4 dash stamps a frame it receives
when reception ends, and a frame it sends when it hands it to the
controller, before it's on the wire; every frame is therefore moved to its
end on the wire (sent ones by one frame time), and a gap is measured from
one frame's end to the next one's start (one frame time before its end).
FRAME_US is an 8-byte frame at 500 kbit/s with typical bit stuffing, so
any one figure is good to about +/-0.05 ms.
"""

from __future__ import annotations

import csv
import json
import statistics
from dataclasses import dataclass, field
from pathlib import Path

FRAME_US = 230.0
NRC_PENDING = 0x78


@dataclass
class Frame:
    t: float  # µs, end of frame on the wire
    sent_by_v4: bool
    id: int
    data: bytes

    @property
    def pci(self) -> int:
        return self.data[0] >> 4 if self.data else -1


@dataclass
class Exchange:
    module: int  # request id
    req_start: float
    req_end: float = 0.0
    dids: tuple[int, ...] = ()
    request_frames: int = 0
    first_answer: float | None = None  # first frame that isn't a 0x78
    answer_end: float | None = None
    answer_frames: int = 0
    pendings: int = 0
    nrc: int | None = None
    positive: bool = False
    # requester flow control: (frame it answers, FC) times
    fc_pairs: list[tuple[float, float]] = field(default_factory=list)

    @property
    def done(self) -> bool:
        return self.answer_end is not None


def load_frames(path: Path) -> list[Frame]:
    frames: list[Frame] = []
    wrap = 0
    last = None
    with path.open() as f:
        for row in csv.DictReader(f):
            t = int(row["t_us"])
            if last is not None and t + wrap < last - 2**31:
                wrap += 2**32  # micros() wrapped (~71.6 min)
            t += wrap
            last = t
            sent = row["dir"] == "T"
            frames.append(
                Frame(
                    t=t + (FRAME_US if sent else 0.0),
                    sent_by_v4=sent,
                    id=int(row["id"], 16),
                    data=bytes.fromhex(row["data"]),
                )
            )
    frames.sort(key=lambda fr: fr.t)
    return frames


def _parse_dids(sdu: bytes) -> tuple[int, ...]:
    if len(sdu) < 3 or sdu[0] != 0x22:
        return ()
    return tuple((sdu[i] << 8) | sdu[i + 1] for i in range(1, len(sdu) - 1, 2))


def rebuild(frames: list[Frame], modules: dict[int, int]) -> list[Exchange]:
    """Exchanges, in request order. `modules` maps request id -> answer id.
    A request superseded before its answer finished counts as unanswered."""
    by_rx = {rx: tx for tx, rx in modules.items()}
    open_: dict[int, Exchange] = {}
    req_buf: dict[int, bytearray] = {}
    req_total: dict[int, int] = {}
    last_answer_frame: dict[int, float] = {}
    out: list[Exchange] = []

    for fr in frames:
        if fr.id in modules:
            tx = fr.id
            pci = fr.pci
            ex = open_.get(tx)
            if pci == 0 or pci == 1:
                ex = Exchange(module=tx, req_start=fr.t - FRAME_US, req_end=fr.t, request_frames=1)
                open_[tx] = ex
                out.append(ex)
                if pci == 0:
                    n = fr.data[0] & 0x0F
                    ex.dids = _parse_dids(fr.data[1 : 1 + n])
                else:
                    req_total[tx] = ((fr.data[0] & 0x0F) << 8) | fr.data[1]
                    req_buf[tx] = bytearray(fr.data[2:8])
            elif pci == 2 and ex is not None and not ex.done and tx in req_buf:
                ex.req_end = fr.t
                ex.request_frames += 1
                req_buf[tx].extend(fr.data[1:8])
                if len(req_buf[tx]) >= req_total[tx]:
                    ex.dids = _parse_dids(bytes(req_buf[tx][: req_total[tx]]))
                    del req_buf[tx]
            elif pci == 3 and ex is not None and tx in last_answer_frame:
                ex.fc_pairs.append((last_answer_frame[tx], fr.t - FRAME_US))
            continue

        tx = by_rx.get(fr.id)
        if tx is None:
            continue
        ex = open_.get(tx)
        if ex is None or ex.done:
            continue
        pci = fr.pci
        if pci == 3:
            continue  # the module's flow control for a long request
        last_answer_frame[tx] = fr.t
        if pci == 0:
            n = fr.data[0] & 0x0F
            sdu = fr.data[1 : 1 + n]
            if len(sdu) >= 3 and sdu[0] == 0x7F and sdu[2] == NRC_PENDING:
                ex.pendings += 1
                continue
            ex.first_answer = fr.t - FRAME_US
            ex.answer_end = fr.t
            ex.answer_frames = 1
            ex.positive = len(sdu) > 0 and sdu[0] == 0x62
            ex.nrc = sdu[2] if len(sdu) >= 3 and sdu[0] == 0x7F else None
        elif pci == 1:
            ex.first_answer = fr.t - FRAME_US
            ex.answer_frames = 1
            ex.positive = len(fr.data) > 2 and fr.data[2] == 0x62
            req_total[-tx] = ((fr.data[0] & 0x0F) << 8) | fr.data[1]
            req_buf[-tx] = bytearray(fr.data[2:8])
        elif pci == 2 and -tx in req_buf:
            ex.answer_frames += 1
            req_buf[-tx].extend(fr.data[1:8])
            if len(req_buf[-tx]) >= req_total[-tx]:
                ex.answer_end = fr.t
                del req_buf[-tx]

    return out


def _stats(values: list[float]) -> dict[str, float] | None:
    if not values:
        return None
    ordered = sorted(values)

    def pct(p: float) -> float:
        return ordered[min(len(ordered) - 1, int(round(p * (len(ordered) - 1))))]

    return {
        "n": len(values),
        "mean_ms": statistics.fmean(values) / 1000,
        "p50_ms": pct(0.50) / 1000,
        "p95_ms": pct(0.95) / 1000,
        "max_ms": ordered[-1] / 1000,
    }


def summarize(frames: list[Frame], exchanges: list[Exchange]) -> dict:
    """Overall and per-module numbers, plus latency by DID set."""
    if not frames:
        return {"frames": 0}
    span_s = max((frames[-1].t - frames[0].t) / 1e6, 1e-6)

    def block(exs: list[Exchange]) -> dict:
        done = [e for e in exs if e.done]
        positive = [e for e in done if e.positive]
        nrcs: dict[str, int] = {}
        for e in done:
            if e.nrc is not None:
                key = f"{e.nrc:02X}"
                nrcs[key] = nrcs.get(key, 0) + 1

        responder = [e.first_answer - e.req_end for e in done if e.first_answer is not None]
        whole = [e.answer_end - e.req_start for e in done]
        requester: list[float] = []
        for prev, nxt in zip(exs, exs[1:]):
            if prev.done and prev.module == nxt.module:
                requester.append(nxt.req_start - prev.answer_end)
        fc = [fc - frame for e in done for frame, fc in e.fc_pairs]

        return {
            "requests": len(exs),
            "answered": len(positive),
            "nrcs": nrcs,
            "unanswered": len(exs) - len(done),
            "pendings": sum(e.pendings for e in exs),
            "answered_per_s": len(positive) / span_s,
            "dids_per_s": sum(len(e.dids) for e in positive) / span_s,
            "responder_turnaround": _stats(responder),
            "requester_turnaround": _stats(requester),
            "requester_fc_turnaround": _stats(fc),
            "exchange_time": _stats(whole),
        }

    by_module: dict[str, list[Exchange]] = {}
    by_dids: dict[str, list[Exchange]] = {}
    for e in exchanges:
        by_module.setdefault(f"{e.module:03X}", []).append(e)
        key = f"{e.module:03X}:" + "+".join(f"{d:04X}" for d in e.dids)
        by_dids.setdefault(key, []).append(e)

    return {
        "frames": len(frames),
        "span_s": span_s,
        "frames_per_s": len(frames) / span_s,
        "bus_load": len(frames) * FRAME_US / 1e6 / span_s,
        "overall": block(exchanges),
        "modules": {k: block(v) for k, v in sorted(by_module.items())},
        "dids": {
            k: _stats([e.first_answer - e.req_end for e in v if e.done and e.first_answer is not None])
            for k, v in sorted(by_dids.items())
        },
    }


def _fmt(s: dict | None) -> str:
    if not s:
        return "-"
    return f"{s['p50_ms']:.2f} / {s['p95_ms']:.2f} / {s['max_ms']:.2f}"


def render(run: dict, summary: dict, extra: str = "") -> str:
    """One-screen markdown summary of a run."""
    o = summary.get("overall")
    lines = [f"# {run.get('label') or run.get('kind', 'run')}", ""]
    lines.append(f"- Kind: {run.get('kind')}, v4 as {run.get('v4_role')}")
    for key in ("latencies_us", "forced_len", "flood", "jobs", "fc", "timeout_ms", "gap_us"):
        if key in run and run[key] not in (None, [], ""):
            lines.append(f"- {key}: {run[key]}")
    if not o:
        lines += ["", "No frames captured."]
        return "\n".join(lines) + "\n"

    lines += [
        f"- {summary['frames']} frames over {summary['span_s']:.1f} s "
        f"({summary['frames_per_s']:.0f}/s, bus ~{summary['bus_load'] * 100:.0f}% busy)",
        "",
        "Turnarounds are p50 / p95 / max, in ms.",
        "",
        "| Module | Answered/s | DIDs/s | Requester | Requester FC | Responder | Unanswered | NRCs | 0x78s |",
        "|---|---|---|---|---|---|---|---|---|",
    ]

    def row(name: str, b: dict) -> str:
        nrcs = ", ".join(f"{k}x{v}" for k, v in b["nrcs"].items()) or "-"
        return (
            f"| {name} | {b['answered_per_s']:.1f} | {b['dids_per_s']:.1f} | "
            f"{_fmt(b['requester_turnaround'])} | {_fmt(b['requester_fc_turnaround'])} | "
            f"{_fmt(b['responder_turnaround'])} | {b['unanswered']} | {nrcs} | {b['pendings']} |"
        )

    for name, b in summary["modules"].items():
        lines.append(row(name, b))
    if len(summary["modules"]) > 1:
        lines.append(row("**All**", o))

    req = o["requester_turnaround"]
    resp = o["responder_turnaround"]
    if req and resp:
        slow = run.get("v4_role") == "responder" and "requester" or "responder"
        v4_side = resp if run.get("v4_role") == "responder" else req
        other = req if run.get("v4_role") == "responder" else resp
        share = v4_side["mean_ms"] / max(v4_side["mean_ms"] + other["mean_ms"], 1e-9)
        lines += [
            "",
            f"v4's share of each cycle: {share * 100:.1f}% "
            f"({'OK' if share < 0.05 else 'TOO HIGH -- the instrument may be the bottleneck'}). "
            f"The device under test is the {slow}.",
        ]

    if extra:
        lines += ["", extra]
    return "\n".join(lines) + "\n"


def did_table(summary: dict) -> str:
    """Responder turnaround by module and DID set -- test C2's table."""
    lines = ["| Module:DIDs | n | p50 | p95 | max |", "|---|---|---|---|---|"]
    for key, s in summary["dids"].items():
        if s:
            lines.append(f"| {key} | {s['n']} | {s['p50_ms']:.2f} | {s['p95_ms']:.2f} | {s['max_ms']:.2f} |")
    return "\n".join(lines) + "\n"


def analyze_dir(run_dir: Path) -> tuple[dict, str]:
    """Analyzes `run_dir` (frames.csv + run.json), writes summary.json and
    summary.md there, and returns them."""
    run = json.loads((run_dir / "run.json").read_text())
    modules = {int(tx, 16): int(rx, 16) for tx, rx in run["modules"].items()}
    frames = load_frames(run_dir / "frames.csv")
    summary = summarize(frames, rebuild(frames, modules))

    extra = ""
    dash = run_dir / "dash.json"
    if dash.exists():
        extra = render_dash(json.loads(dash.read_text()))
    if run.get("did_table"):
        extra += "\n" + did_table(summary)

    text = render(run, summary, extra)
    (run_dir / "summary.json").write_text(json.dumps(summary, indent=2))
    (run_dir / "summary.md").write_text(text)
    return summary, text


LOOP_BUCKETS_MS = ["<1", "1-2", "2-5", "5-10", "10-20", "20-50", "50-100", ">=100"]


def render_dash(dash: dict) -> str:
    """The v5 dash's own POLLSTATS/LOOPSTATS, as markdown."""
    poll = dash.get("poll", {})
    loop = dash.get("loop", {})
    lines = ["## v5 dash"]
    if poll:
        elapsed = max(int(poll.get("elapsed_ms", "0")), 1)
        lines.append(
            f"- POLLSTATS over {elapsed / 1000:.1f} s: {poll.get('sent')} sent, "
            f"{poll.get('answered')} answered ({int(poll.get('answered', 0)) * 1000 / elapsed:.1f}/s), "
            f"{poll.get('timeouts')} timeouts, {poll.get('nrcs')} NRCs, flood={poll.get('flood')}"
        )
    if loop:
        hist = loop.get("hist", "").split(",")
        loops = max(int(loop.get("loops", "0")), 1)
        elapsed = max(int(loop.get("elapsed_ms", "0")), 1)
        lines.append(
            f"- Loop: {loops * 1000 / elapsed:.0f} passes/s, longest {int(loop.get('period_max_us', 0)) / 1000:.1f} ms; "
            f"longest LVGL {int(loop.get('lvgl_max_us', 0)) / 1000:.1f} ms, "
            f"longest scheduler (logger) {int(loop.get('sched_max_us', 0)) / 1000:.1f} ms"
        )
        if len(hist) == len(LOOP_BUCKETS_MS):
            lines.append(
                "- Loop period histogram (ms): "
                + ", ".join(f"{b}: {h}" for b, h in zip(LOOP_BUCKETS_MS, hist))
            )
        if loop.get("can") == "1":
            lines.append(
                f"- TWAI: rx_missed {loop.get('rx_missed')}, rx_overrun {loop.get('rx_overrun')}, "
                f"bus_errors {loop.get('bus_errors')}, tx_failed {loop.get('tx_failed')}"
            )
    entries = dash.get("entries", [])
    if entries:
        lines += [
            "",
            "| Request | Target ms | Answers | Mean ms | Max ms | On target |",
            "|---|---|---|---|---|---|",
        ]
        for e in entries:
            answers = int(e.get("answers", 0))
            target = int(e.get("period_ms", 0))
            mean = int(e.get("interval_sum_ms", 0)) / (answers - 1) if answers > 1 else 0.0
            flag = "-" if answers < 2 else ("yes" if mean <= target * 1.1 else "**no**")
            prio = "" if e.get("prio") == "1" else " (log)"
            lines.append(
                f"| {e.get('ecu')}:{e.get('did')}{prio} | {target} | {answers} | {mean:.1f} | "
                f"{e.get('interval_max_ms')} | {flag} |"
            )
    return "\n".join(lines) + "\n"


def selftest() -> None:
    """A made-up log with known turnarounds, through rebuild/summarize."""
    modules = {0x7E0: 0x7E8}
    frames: list[Frame] = []
    t = 1000.0
    for _ in range(10):
        # request (dash, received) -> 2 ms -> single-frame answer (v4, sent)
        frames.append(Frame(t, False, 0x7E0, bytes([3, 0x22, 0xF4, 0x0C, 0, 0, 0, 0])))
        t += 2000 + FRAME_US
        frames.append(Frame(t, True, 0x7E8, bytes([5, 0x62, 0xF4, 0x0C, 1, 2, 0, 0])))
        t += 5000 + FRAME_US  # requester thinks for 5 ms
    # a 3-DID read answered in two frames, the requester's FC 1 ms after the FF
    frames.append(Frame(t, False, 0x7E0, bytes([7, 0x22, 0xF4, 0x0C, 0xF4, 0x05, 0x20, 0x6E])))
    t += 1000 + FRAME_US
    frames.append(Frame(t, True, 0x7E8, bytes([0x10, 13, 0x62, 0xF4, 0x0C, 1, 2, 0xF4])))
    ff_end = t
    t += 1000 + FRAME_US
    frames.append(Frame(t, False, 0x7E0, bytes([0x30, 3, 0, 0, 0, 0, 0, 0])))
    t += 100 + FRAME_US
    frames.append(Frame(t, True, 0x7E8, bytes([0x21, 0x05, 3, 4, 0x20, 0x6E, 5, 6])))

    exs = rebuild(frames, modules)
    s = summarize(frames, exs)["overall"]
    assert len(exs) == 11 and s["answered"] == 11, s
    assert abs(s["responder_turnaround"]["p50_ms"] - 2.0) < 0.01, s["responder_turnaround"]
    assert abs(s["requester_turnaround"]["p50_ms"] - 5.0) < 0.01, s["requester_turnaround"]
    assert abs(s["requester_fc_turnaround"]["max_ms"] - 1.0) < 0.01, s["requester_fc_turnaround"]
    assert exs[-1].dids == (0xF40C, 0xF405, 0x206E) and exs[-1].answer_frames == 2
    assert exs[-1].fc_pairs[0][0] == ff_end
    print("analyze selftest OK")
