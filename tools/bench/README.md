# tools/bench

Runs the throughput tests in [THROUGHPUT_TEST_PLAN.md](../../THROUGHPUT_TEST_PLAN.md) from the Mac and analyzes them. The **v4 dash** is the instrument: in its bench mode it answers as the car (Phase 1), asks as the dash (Phase 2) or only listens (Phase 3), and streams every frame on the bus to the Mac with a microsecond timestamp. The **v5 dash** is the device under test in Phase 1. Its `POLLSTATS`/`LOOPSTATS` commands report what it saw from the inside.

Needs Python 3.10+ with pyserial; the monitor's virtualenv has it:

```sh
cd tools/bench
../monitor/.venv/bin/python run.py selftest      # checks the analysis, no hardware
```

Every run asks v4 for bench mode first, so **tap Start on v4's screen** when it asks. v4 has to be on the gauges for the request to work. The run ends v4's bench mode when it finishes, and v4 also drops back to the gauges if its USB cable is unplugged. Ports are the `/dev/cu.usbmodem…` names: v4 is the Teensy, and v5 is the ESP32-S3's USB serial.

## Phase 1: the v5 dash on the bench

Put both dashes on the bench OBD socket. Set v5's panels and logging by hand for the configuration under test (Light, Heavy or Worst realistic), then run:

```sh
run.py dash --v4 /dev/cu.usbmodemA --v5 /dev/cu.usbmodemB --label heavy
```

| Test | Command |
|---|---|
| 1B, 1C: ceiling | `dash ... --label <config>` (flood is the default) |
| 1C: natural demand | `dash ... --no-flood --label <config>_natural` |
| 1D: latency sweep | `dash ... --latency 0,2000,5000,10000,20000,40000 --label worst` |
| 1E: multi-frame answers | `dash ... --forced-len 6,13,27 --label mf` (2, 3 and 5 frames) |
| 1F: endurance | `dash ... --no-flood --duration 1800 --latency <car p50 µs> --label endurance` |

Test 1A uses `env:dash_s3_stress` on v5 alone. There's no v4 and no wire, so read `LOOPSTATS`/`POLLSTATS` over v5's serial console directly after `POLLFLOOD 1`, `STATSRESET` and a 60 s wait.

Each run configures v4 with every compiled module and the length each DID answers with (from `signals.json`), waits `--settle` seconds, resets v5's stats, records `--duration` seconds, then saves v5's stats beside the frame log.

## Phase 2: the car

v4 on the car's OBD port, v5 and OBDEleven unplugged. Everything is read-only: `22`, plus `10 03`/`10 01` in C0 only.

| Test | Command |
|---|---|
| C0: plain reads | `car c0 --v4 PORT` |
| C1: one module flat out | `car c1 --v4 PORT [--module 7E0,7E1] --label idle` |
| C2: latency by DID | `car c2 --v4 PORT` |
| C3: multi-DID | `car c3 --v4 PORT [--module 7E1] [--n 1,2,3]`, then the best N again with `--fc-bs 3`, `--fc-stmin 1`… |
| C4a: adding modules | `car c4 --v4 PORT` |
| C4b: two in flight to one module | `car run --v4 PORT --job 0:7E0:F40C --job 1:7E0:F40C` |
| C5: conditions | C1/C3 again with `--label off` / `idle` / `3000rpm` |
| C6: endurance | `car run --v4 PORT --duration 600 --job 0:7E0:<3 DIDs> --job 0:7E0:<3 more> …` |

A `--job` is `LANE:MODULE:DID,DID,…` (hex). A lane keeps one request in flight and cycles through its jobs, and lanes run at the same time. `--timeout-ms` (default 100), `--fc-bs`/`--fc-stmin` (the flow control v4 sends for long answers) and `--gap-us` (pause between requests) apply to every car test.

## Phase 3: cross-check

```sh
run.py listen --v4 PORT --duration 120 --label v5_in_car
```

v4 only listens, beside the v5 dash in the car.

## Results

Each run writes a folder under `results/` (gitignored), named by time and label:

- `frames.csv`: every frame, `t_us,dir,id,data`. `dir` is `R` for a frame v4 received and `T` for one it sent.
- `run.json`: what was configured, plus v4's counters.
- `dash.json` / `dash.txt`: v5's `POLLSTATS` + `LOOPSTATS` (Phase 1).
- `summary.md` / `summary.json`: the analysis. `run.py analyze <folder>` redoes it.

The summary rebuilds every exchange from the wire and splits each cycle into its halves:

- **Requester turnaround:** from an answer's last frame to the next request.
- **Requester FC:** from a long answer's first frame (or block) to the requester's flow control.
- **Responder turnaround:** from a request's last frame to the answer's first.

Times are corrected to frame ends on the wire, assuming 0.23 ms per 8-byte frame at 500 kbit/s, so each figure is good to about ±0.05 ms. "v4's share of each cycle" should be under 5%; above that, the instrument may be what's limiting the run.

## Layout

| File | Role |
|---|---|
| `run.py` | CLI: the tests above |
| `mqb_bench/link.py` | serial links: v4's bench mode, v5's stats commands |
| `mqb_bench/catalog.py` | modules, DIDs and answer lengths from `signals.json` (via `tools/monitor`'s loader) |
| `mqb_bench/analyze.py` | frame log → exchanges → summary |
