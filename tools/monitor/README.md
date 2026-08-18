# MQB Monitor

A macOS UDS CAN monitor/decoder for the MQB dashboard project. It sits
passively on the CAN bus between an **OBDEleven** and the car, watches the
diagnostic request/response traffic, and decodes it against the **same signal
registry the firmware compiles from**
([`core/vehicle/data/signals.json`](../../core/vehicle/data/signals.json)).
It's also the primary tool for growing and reviewing that registry: discover
new UDS DIDs, save partial progress as WIP, and promote/demote/edit any
signal without touching JSON by hand.

There is no good passive UDS decoder for macOS, and a full CANbus suite is
overkill — this is purpose-built for the one job: understand and grow the
project's UDS signal registry.

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
   Wire the Teensy's CAN1 transceiver (pins **22 CTX1 / 23 CRX1**) across the
   bus between the OBDEleven and the car — a different peripheral/pins than
   the display firmware's CAN3, since this is a separate standalone Teensy 4.0
   board, not the display's Teensy MicroMod. The firmware opens
   **listen-only**, so the Teensy never ACKs or transmits — it's a safe
   passive tap.

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

## The registry: active vs. WIP

Every signal ever defined lives in one file, `signals.json`, tagged with a
`status`:

- **Active** — compiled into the firmware (`tools/gen_catalog.py` filters to
  active-only). Shows up in the Signals watchlist picker, and the live
  engine decodes its DID automatically wherever it appears in Traffic.
- **WIP** — not compiled, and not auto-decoded. The live engine treats a WIP
  signal's DID exactly like an undiscovered one ("unknown, log it raw") until
  you flip it to Active — so a guessed offset/scale never quietly shows up
  looking as trustworthy as a confirmed one.

Nothing is destroyed by demoting a signal to WIP, and nothing reaches the
firmware by creating one WIP — you have to explicitly set Status to Active
and (usually) hit Regenerate.

## Compound signals

A **compound** (a.k.a. composite) signal is computed from two others rather
than read directly off the bus — the existing example is "Int Pres Act."
(actual intake pressure = intake pressure − ambient pressure). Registry's
**New Compound Signal…** builds one of these without writing any C++: pick
two existing *direct* signals (A and B) already in the registry, pick how to
combine them (`A + B`, `B − A`, average, min, or max), and it copies each
source's own already-calibrated request (DID + offset/scale) into the new
signal — nothing to re-derive. The result behaves exactly like any other
registry entry (Status toggle, Delete, etc.) but is excluded from the
Signals watchlist picker, same as any composite signal.

For a combination that isn't a straight sum/difference/average/min/max —
something more bespoke — hand-write a new `transform_*` function in
`signal_catalog.cpp` (and its Python mirror in `catalog.py`'s `TRANSFORMS`)
and reference it by name in `signals.json`, same as `actual_intake_pressure`
today.

## Reordering

Registry's **Move Up**/**Move Down** swap a signal with its neighbor in the
registry's own list order — the same order `gen_catalog.py` compiles active
entries into, so reordering active signals changes the firmware's
`SIGNAL_CATALOG[]` array order too. Only enabled with the filter set to
"All", since a filtered view skips entries and isn't the registry's real,
contiguous order.

This is safe today because no device has shipped with saved config yet
(`ConfigStore::migrate()` is currently a no-op — see
[core/vehicle/README.md](../../core/vehicle/README.md)). Once one has,
`Config::signals[]` in flash is indexed by this same compiled order, so
reordering active signals after that point would misalign already-saved
per-signal warning thresholds on any device that already updated — a
migration would need to handle that first.

## The five tabs

- **Signals** — a **watchlist you build yourself**: starts empty; use *Add
  signals…* to pick which *active* catalog signals to watch (and *Remove
  selected* to drop them), so you can focus on just the ones you care about.
  Each is decoded live to engineering units (toggle metric/imperial), with a
  fill bar over its display range, update count, response latency, and age.
  Your selection persists across launches, and a freshly activated signal is
  added automatically.
- **Traffic** — a rolling, decoded UDS message log, **newest at the top** —
  scroll down for history. Requests, responses (with the decoded signal
  value), negative responses, ISO-TP multi-frame count, and per-response
  latency. Filter by known/unknown/request/response; pause and auto-scroll
  (auto-scroll off freezes the view without losing anything — the underlying
  capture keeps running). **Save Log…** exports to CSV: decoded messages
  (honors the filter) or raw CAN frames (everything).
- **Registry** — every signal, active and WIP alike, in one table — the
  birds-eye view for reviewing what's actually validated. Filter (All/Active
  only/WIP only), **Toggle Status** for a fast bulk flip without opening the
  editor, **Edit…** for anything else, **New Signal…** to hand-enter one you
  already know about, **New Compound Signal…** to build a computed signal
  from two existing direct ones (sum/difference/average/min/max — see below),
  **Move Up**/**Move Down** to reorder (filter must be "All" — see below),
  **Delete**, and **Regenerate Firmware Catalog** to sync
  `signal_catalog.generated.inc` once you're done reviewing a batch.
- **Learn** — every **unknown** DID seen on the bus (an OBDEleven query with
  no active-signal match). Pick one and click **Add to Catalog / Save WIP…**
  to open the signal editor: name it, pick a unit, solve `offset`/`scale`
  from two `raw → true value` points (read the "true value" off the
  OBDEleven display), and choose **Status: Active** to commit it or **WIP**
  if you're not confident yet.
- **WIP** — every WIP-status signal, for resuming (**Resume…** reopens the
  editor with everything you saved pre-filled, plus any live samples if the
  DID is currently on the bus) or **Delete** if it turned out to be a dead
  end. Nothing here reaches the compiled catalog until its status is flipped
  to Active.

The signal editor (opened from Learn/WIP/Registry) is one dialog with a
**Status** combo (Active/WIP) and a single Save button — that combo *is* the
two-stage toggle, whichever tab you opened it from.

## Passive vs. active

By default everything is **passive** — the app only listens. `--active` opens
the bus in normal mode so a later probing feature can transmit its own `0x22`
requests to sweep for DIDs. Only use it when you intend to participate on the
bus; passive monitoring between the OBDEleven and the car needs listen-only.

## Layout

| File | Role |
|------|------|
| `run.py` | entry point / CLI |
| `mqb_monitor/catalog.py` | loads/saves the `signals.json` registry; **firmware-accurate** decode; active-only DID index |
| `mqb_monitor/uds.py` | ISO-TP reassembly + UDS message parsing |
| `mqb_monitor/sources.py` | SLCAN serial source + hardware-free demo source |
| `mqb_monitor/session.py` | the engine: frames → decode → signal/unknown state (thread-safe, GUI-agnostic) |
| `mqb_monitor/theme.py` | dark/blue-accent Fusion theme (palette + QSS) |
| `mqb_monitor/gui.py` | the PySide6 window: sidebar shell + five tabs + the signal editor dialog |

The engine (`session.py`) is deliberately GUI-free and is what `--selftest`
exercises, so decode/ISO-TP/registry logic can be tested without a display.
