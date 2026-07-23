# MQB Monitor

A macOS UDS CAN monitor/decoder for the MQB dashboard project. It sits
passively on the CAN bus between an **OBDEleven** and the car, watches the
diagnostic request/response traffic, and decodes it against the **same signal
catalog the firmware uses** ([`core/vehicle/data/signals.json`](../../core/vehicle/data/signals.json)).
It also helps you **discover and label new UDS DIDs** and write them straight
back into that catalog.

There is no good passive UDS decoder for macOS, and a full CANbus suite is
overkill — this is purpose-built for the one job: understand and grow the
project's UDS catalog.

## How it fits together

```
  car (MQB ECU) ⇄ CAN 500k ⇄ OBDEleven
        │  tap CAN-H / CAN-L
   Teensy 4.0 (env:can_sniffer firmware, listen-only)
        │  USB serial (SLCAN)
   this app  ──reads/writes──►  core/vehicle/data/signals.json
```

1. Flash the sniffer firmware onto a **Teensy 4.0** (separate from the display
   board):
   ```sh
   pio run -e can_sniffer -t upload
   ```
   Wire the Teensy's CAN3 transceiver (pins **30 CRX3 / 31 CTX3**) across the
   bus between the OBDEleven and the car. The firmware opens **listen-only**,
   so the Teensy never ACKs or transmits — it's a safe passive tap.

2. Install and run the app:
   ```sh
   cd tools/monitor
   python3 -m venv .venv
   .venv/bin/pip install -r requirements.txt
   .venv/bin/python run.py            # auto-detects the Teensy serial port
   ```

3. In the OBDEleven app, open live data / adaptation for the values you care
   about. Their queries appear here decoded in real time.

## Running

```sh
.venv/bin/python run.py               # GUI, auto-detect port
.venv/bin/python run.py --port /dev/tty.usbmodemXXXX
.venv/bin/python run.py --demo        # GUI on synthesized traffic, no hardware
.venv/bin/python run.py --selftest    # headless engine check (needs only pyserial)
.venv/bin/python run.py --active      # open the bus in NORMAL mode (can TX) — see below
```

`--demo` synthesizes realistic OBDEleven-style scan traffic (including an
unknown DID and a multi-frame response) so you can explore the whole UI with
nothing plugged in.

## The three tabs

- **Signals** — a **watchlist you build yourself**: starts empty; use *Add
  signals…* to pick which catalog signals to watch (and *Remove selected* to
  drop them), so you can focus on just the ones you care about. Each is decoded
  live to engineering units (toggle metric/imperial), with a fill bar over its
  display range, update count, response latency, and age. Your selection
  persists across launches, and a freshly learned signal is added
  automatically.
- **Traffic** — a rolling, decoded UDS message log: requests, responses (with
  the decoded signal value), negative responses, ISO-TP multi-frame count, and
  per-response latency. Filter by known/unknown/request/response; pause and
  auto-scroll.
- **Learn** — every **unknown** DID seen on the bus (an OBDEleven query with no
  catalog match). Pick one, and the **Add to Catalog** dialog lets you name it,
  choose a unit, and solve `offset`/`scale` from two `raw → true value` points
  (read the "true value" off the OBDEleven display). Saving appends it to
  `signals.json` and optionally re-runs `gen_catalog.py` so the firmware picks
  it up on the next build.

## Passive vs. active

By default everything is **passive** — the app only listens. `--active` opens
the bus in normal mode so a later probing feature can transmit its own `0x22`
requests to sweep for DIDs. Only use it when you intend to participate on the
bus; passive monitoring between the OBDEleven and the car needs listen-only.

## Layout

| File | Role |
|------|------|
| `run.py` | entry point / CLI |
| `mqb_monitor/catalog.py` | loads `signals.json`; **firmware-accurate** decode + write-back |
| `mqb_monitor/uds.py` | ISO-TP reassembly + UDS message parsing |
| `mqb_monitor/sources.py` | SLCAN serial source + hardware-free demo source |
| `mqb_monitor/session.py` | the engine: frames → decode → signal/unknown state (thread-safe, GUI-agnostic) |
| `mqb_monitor/gui.py` | the PySide6 window and three tabs |

The engine (`session.py`) is deliberately GUI-free and is what `--selftest`
exercises, so decode/ISO-TP/learn logic can be tested without a display.
