# Throughput characterization plan

Two separate questions, tested separately:

1. **The dash.** How many UDS requests per second the **v5 dash** (ESP32-S3 PCB, `env:dash_s3`) can send, answer and log while drawing the screen, if the ECU answering it were infinitely fast. This sets `BUS_BUDGET_RPS` and is the same for every car.
2. **The car.** How many requests per second the car (gateway plus each module) can answer from the OBD port, if the requester were infinitely fast. This includes multi-DID reads (`22 DID1 DID2 DID3`).

Each test pairs the device under test with a requester or responder that is provably not the bottleneck. A combined v5-in-car run comes last, only as a cross-check of the prediction.

**The instrument is the v4 dash** (Teensy MicroMod + C3, `env:dash_teensy`) in a new bench mode, under control of the Mac over USB. In Phase 1 it plays the car for the v5 dash; in Phase 2 it plays the dash for the car. v4 is only the test instrument; its own throughput isn't being characterized. The fallback instrument, if v4 can't do it, is the ESP32-S3 dev kit.

## How a run shows which side is the bottleneck

The dash keeps one request in flight at a time, so every exchange is a closed loop:

```
requester sends ─► [responder turnaround] ─► response complete ─► [requester turnaround] ─► next request
```

The v4 dash sits on the bus and timestamps every frame in microseconds, its own and other nodes' alike. Every run is therefore split into two numbers taken from the wire, with no clock sync and no trust in the device's own reporting:

- **Responder turnaround:** from the request frame to the response's first frame (or last frame, for multi-frame).
- **Requester turnaround:** from the response's last frame to the next request.

In the dash test v4 is the responder, and its turnaround should be tens of µs. In the car test it is the requester, with the same requirement. If v4's turnaround is ever more than a few percent of the cycle, the run is invalid.

## What the code does today (relevant to the numbers)

- **One request per main-loop pass.** `loop()` runs `uds_client.poll()`, then `uds_poller.tick()`, then `lv_timer_handler()` on one thread ([src/main.cpp](src/main.cpp)). A response that lands during a repaint waits for the next pass, so the dash's turnaround is at least the remaining loop time. The dash's ceiling is probably the loop rate, not CAN; Phase 1 tests this.
- **Multi-frame answers cost loop passes too.** `UdsClient::poll()` sends flow control (`30 03 00`) when it drains a first frame, and again every 3 consecutive frames. Each flow control waits for a pass, so a 2-frame multi-DID answer can cost an extra loop iteration on the dash.
- **LVGL refresh period is 20 ms.** The S3 renders in bands from internal RAM.
- **Natural demand today:** the catalog's 50 compiled signals have cycle times of 50 ms (5 signals), 100 (20), 500 (12), 1000 (8), 2000 (1) and 5000 (5). The worst case, 6 panels plus a 16-signal profile, can't exceed about 270 req/s. So the ceiling test needs a flood mode (every period 0), not just a heavy config.
- **Why 3 DIDs is the sweet spot:** `22` plus three 2-byte DIDs is 7 bytes, the most a single CAN frame can carry. A fourth DID makes the request multi-frame, so the tester has to wait for the ECU's flow control before sending the rest: an extra round trip on every request. The answer to a 3-DID read (13+ bytes) is multi-frame regardless.
- **Bus ceiling:** at 500 kbit/s a classic 8-byte frame is about 0.25 ms on the wire, so one request in flight tops out near 2000 single-frame exchanges per second. Nothing here should get close to that; if a run does, bus occupancy belongs in the report.

## Equipment

- **Device under test:** the v5 dash (S3 PCB, `env:dash_s3`) on the bench OBD socket (12 V on pin 16, ground on 4/5, CAN-H on 6, CAN-L on 14, 120 Ω at each end).
- **Instrument:** the v4 dash (`env:dash_teensy`, CAN3), with USB to the Mac, on the same bench bus for Phase 1 and on the car's OBD port for Phase 2. It plugs into the car's OBD port the same way it does as a dash. Remove the bench terminator for the car, since the car is already terminated.

## Tooling (built 2026-11-12; not yet run on hardware)

### T1: Dash stats (`src/main.cpp`, USB serial, next to `LOGSTATUS`)

In the normal build of both dashes, so the firmware measured is the firmware that ships. Every reply is `key=value`.

- `POLLSTATS`: since the last reset, requests sent, answered, timed out, rejected (final NRCs) and 0x78s, and whether flood is on. One `E` line per request-table entry: target period, answers, interval sum and longest interval (mean = sum / (answers − 1)). This achieved-vs-target table is the user-facing answer to "do the panels update as fast as they claim". Then `END`.
- `LOOPSTATS`:
  - loop passes, and the loop period as a histogram (<1, 1–2, 2–5, 5–10, 10–20, 20–50, 50–100, ≥100 ms) plus its longest
  - the longest `lv_timer_handler()` call
  - the longest scheduler pass, which is where logger flushes happen
  - on v5, TWAI's `rx_missed`, `rx_overrun`, bus errors and failed transmits since the reset
- `STATSRESET` → `OK`.
- `POLLFLOOD 1|0` → `OK`: every entry is due again the moment it's answered, so the poller sends back to back (entries in back-off keep their slow retry). RAM only.

### T2: Bench mode on the v4 dash (`env:dash_teensy`)

Part of v4's normal firmware, so there's no reflashing between being a dash and being the instrument:
- **Entering and leaving:** the Mac sends `BENCHREQ`, the panel asks **Start CAN bench?**, and the dashboard pauses (no polling, no logging). Stop on the panel, `BENCHEND`, or unplugging USB return to the gauges. The screen is sniffer mode's, titled **CAN Bench**, refreshed twice a second.
- **Interrupt-driven:** `core/bench`'s `BenchEngine` runs from the CAN3 receive interrupt and a 50 µs PIT timer at the same priority. Each frame is stamped with `micros()` on arrival. A zero-latency answer goes out from the receive interrupt itself; anything timed (latency, STmin, timeouts) runs from the timer. Redraws can't skew either.
- **Memory:** the engine and its 4096-frame log (80 KB) are allocated in RAM2 on entry and freed on exit. The code is all in flash; ITCM sits 56 bytes under its 32 KB block edge.

Every frame on the bus streams to the Mac as `F <t_us> R|T <id> <data>`. It has three roles:

- **Responder (fake ECU)** for Phase 1:
  - Answers `22` reads on the modules the Mac loads, each DID with its length from `signals.json` (or one forced length).
  - Values change with every answer, so the logger always has something to write.
  - Added latency: one value, or a list replayed in order (e.g. measured car latencies).
  - Multi-DID requests, single- or multi-frame; multi-frame answers that honour the requester's flow control (BS/STmin).
  - Optional 0x78 before every nth answer, or no answer to every nth request.
- **Tester (fake dash)** for Phase 2. It runs its job list on v4 itself, so USB and Python latency never enter the loop:
  - Each job is one module plus 1–6 DIDs. A lane keeps one request in flight and cycles through its jobs; lanes run concurrently (two lanes on one module is test C4b).
  - Closed loop, with a configurable timeout and optional gap.
  - Sends multi-frame requests (FF, wait for the ECU's FC, then CFs) for 4–6 DIDs.
  - Configurable flow control for multi-frame answers.
  - Plus one-off raw frames, for C0.
- **Listen-only** for Phase 3: transmits nothing, with the same µs timestamps. Sniffer mode's SLCAN only has ms timestamps.

### T3: Mac scripts ([tools/bench](tools/bench/README.md))

- `run.py dash` / `run.py car c0…c4|run` / `run.py listen` configure v4 (and v5's flood and stats), run each test for a set duration and save the raw frame log. A comma list of latencies or forced lengths runs a sweep in one session.
- `mqb_bench/analyze.py` rebuilds every exchange from the log and reports the turnaround split, answered/s and DIDs/s, percentiles, NRC/timeout counts, and v4's share of the cycle. It writes a one-screen markdown summary per run.
- Modules, DIDs and answer lengths come from `tools/monitor`'s catalog loader.
- Results go to a gitignored `tools/bench/results/`.

---

## Phase 1: the dash (bench only)

Every test sets a dash configuration, waits 10 s, runs `STATSRESET`, runs 60 s, then collects `POLLSTATS` + `LOOPSTATS` and v4's summary.

**Configurations used below:**

| Name | Panels | Display mode | Logging |
|---|---|---|---|
| Light | 1 | Digital | off |
| Heavy | 6 | Gauge | off |
| Worst realistic | 6 | Gauge | Profile, 16 signals not on screen |

The Wi-Fi AP stays off in every test. While it's up the dash is in configuration and doesn't poll the car, so there's no throughput to measure.

### 1A. Software ceiling (no wire)
`env:dash_s3_stress` with `POLLFLOOD 1`. Its synthetic bus answers in-process with zero latency, so the request rate equals the loop rate. Run Light and Heavy; `DASH_S3_LCD_STATS` gives fps and frame time too. Needs only T1. This gives the first real number and shows how much drawing costs.

### 1B. On the wire, instant responder
`env:dash_s3` on the bench, v4 responder at 0 latency, `POLLFLOOD 1`, Light and Heavy. Compare with 1A: the difference is the cost of TWAI and the wire. v4's dash-turnaround histogram should mirror `LOOPSTATS`' loop period. If it does, the loop is confirmed as the limit.

### 1C. Load factors, one at a time from Heavy
- Logging: off, then Screen, then Profile (16).
- Screen changes mid-run: open Settings, then the Logging screen.

Run each under `POLLFLOOD 1` (ceiling) and `POLLFLOOD 0` (does natural demand still meet every target period?). Watch for the longest logger flush and for a loop stall leading to `rx_missed`.

### 1D. Responder latency sweep
Worst realistic, flood: responder latency of 0, 2, 5, 10, 20 and 40 ms. The result is a curve of dash req/s against ECU latency. It shows whether a response that arrives mid-repaint costs a whole loop period. Combined with Phase 2's measured latencies, it predicts in-car rates without the car.

### 1E. Multi-frame answers
Responder answers of 2, 3 and 5 frames, honouring `30 03 00`. The measurement is the dash's flow-control turnaround (first frame to FC, and each third CF to FC). This is the cost multi-DID reads would add on the dash side.

### 1F. Endurance
Worst realistic config, natural demand, 30 min, responder at the car's median latency once Phase 2 has it (5 ms until then). It passes with zero timeouts, zero `rx_missed`/overrun, no hang-watchdog reset, and every entry's achieved period within 10% of target.

**Phase 1 outputs:**
- Table of dash ceiling (req/s) per configuration.
- `BUS_BUDGET_RPS` = sustained answered req/s in the worst realistic configuration at 0 latency, times a margin. I suggest 0.8; it's your call.
- Whether `BACKGROUND_STARVATION_MS` (1000) holds up: background entries' achieved period in 1C's Profile run.
- The latency curve (1D) and the FC cost (1E).

---

## Phase 2: the car (v4 as tester, no v5)

**Setup and safety:**
- v4 on the OBD port in bench mode, transmitting (it has to ACK).
- v5 dash and OBDEleven unplugged, so v4 is the only diagnostic client.
- Battery maintainer on for engine-off runs.
- Read-only: `22` only. `10 03` and `3E 80` only if C0 shows they're needed. Nothing that writes, codes or resets.
- Scan for DTCs with OBDEleven (gateway and engine) before and after each session.
- All tests run closed loop (next request on answer, NRC or timeout). No open-loop flooding.

### C0. Do plain reads work on every module?
A module starts in the *default* diagnostic session. `10 03` (Diagnostic Session Control, extended session) switches it into a mode with more services unlocked. OBDEleven sends `10 03` to every module before it reads anything, so every DID in `signals.json` was found in the extended session. The dash never sends it; it only sends plain `22` reads, which only the engine is known to answer.

For each module in the registry, read one known DID with a plain `22`.
- **It answers:** nothing changes, and C1–C6 use plain reads on it.
- **It doesn't** (NRC 0x7F/0x31, or a timeout): send `10 03`, read again, and note the module.

Any module that needs `10 03` matters twice:
- It's new dash work, because an extended session times out after a few seconds unless the requester keeps sending Tester Present (`3E 80`), which takes a little of the request budget.
- That module's later tests have to open the session first.

### C1. Single-module ceiling
For each module, one DID the dash actually uses, closed loop, 30 s. Measures car turnaround p50/p95/max, req/s, NRCs and timeouts. Run with the engine off and at idle.

### C2. Latency by DID
For each module, cycle through every compiled DID it owns, 20 reads each. Shows whether some DIDs are much slower (computed values). Gives a latency table per DID.

### C3. Multi-DID (ISO-TP; can be its own session)
Engine module, N = 1 to 6 DIDs per request, 60 s each, using the same DIDs throughout.
- N ≤ 3 is a single-frame request; N = 4–6 needs a multi-frame request.
- Record whether the ECU answers all DIDs, only the first, or with NRC 0x13/0x14/0x31.
- Also record: turnaround, requests/s and **DIDs/s**, the ECU's FC parameters (BS/STmin) for multi-frame requests, and frames per answer.
- Repeat the best N with our FC as `30 00 00`, then `30 03 00` (the dash's), then a non-zero STmin, to see whether the ECU sends faster with fewer FCs.
- Repeat N = 3 on one non-engine module (transmission).

**Hypothesis:** N = 3 gives the most DIDs/s.

### C4. Gateway concurrency
- **a.** One request in flight per module, adding modules one at a time up to all of them. Compare aggregate req/s with the sum of the C1 rates. If it keeps scaling, the gateway isn't the limit; where it stops scaling is the gateway's ceiling. This decides whether "one in flight per module" is worth building on the dash.
- **b.** Optional: two requests in flight to the same module. Expect the second to be ignored or NRC 0x21, which confirms the one-per-module rule.

### C5. Operating conditions
Repeat C1 (engine) and the best C3 at engine off, idle, and a steady 2500–3000 rpm while parked. Optionally repeat on a drive, with a passenger running the laptop. The engine ECU may answer more slowly under load.

### C6. Endurance
The target workload closed loop for 10 min at idle, e.g. 15 engine DIDs as five 3-DID requests (or whatever C3 shows is best). It passes with no drift in turnaround, no NRC 0x21/0x22 build-up, no lockout and no new DTCs.

**Target to confirm:** 12–15 DIDs, each refreshed at least every 100 ms, with a stretch goal of 50 ms. Fifteen DIDs at 20 Hz is 300 DIDs/s, which is 100 three-DID requests/s, or 10 ms per exchange.

**Phase 2 outputs:**
- Turnaround per module, and per DID.
- Multi-DID verdict and the best N, which feeds the TODO.md Multi-DID item.
- Whether the gateway or the modules are the limit, which feeds the per-module in-flight follow-up.
- A latency list to replay through the responder.

---

## Phase 3: cross-check (after any dash changes from 1–2)

1. Replay Phase 2's latencies through the responder (as in 1D) to predict in-car rates for the dash.
2. Put the v5 dash in the car with v4 beside it in bench mode's **listen-only** role, and compare the measured rates and turnaround split with the prediction. A gap means one of the bench models is missing something.

## Decisions

- **Taken:** the T1 commands ship in release firmware, and bench mode lives in v4's normal firmware.
- **Open:** the car target above (100 ms, with 50 ms as the stretch goal) and the `BUS_BUDGET_RPS` margin.
