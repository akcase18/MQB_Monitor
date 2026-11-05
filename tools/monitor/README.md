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
     or the dash itself, in sniffer mode (env:teensymm)
        │  USB serial (SLCAN)
   this app  ──reads/writes──►  core/vehicle/data/signals.json
```

1. Pick a sniffer. **Either** use the dash itself (no extra hardware -- see
   *Sniffing through the dash* below), **or** flash the standalone sniffer
   firmware onto a **Teensy 4.0** (separate from the display board):
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

### Sniffing through the dash

The dash firmware (`env:teensymm`) can stand in for the standalone sniffer:
it's already on the bus, and its USB port speaks the same SLCAN (both use
`core/can`'s `slcan.hpp`). It never switches on its own -- Connect sends
`SNIFFREQ`, the dash pops up **Start CAN sniffer?**, and nothing changes
until the driver taps **Start**. The status bar reads *Confirm on dash…*
meanwhile; Cancel, a 10 s timeout, or the dash being in a menu rather than
on the gauges ends the attempt with the reason shown.

While sniffing, the dash is paused outright -- no UDS polling (so the
capture holds none of its own requests), no logging -- and shows a
**CAN Sniffer** screen with frames/s, total and dropped counts. **Stop** on
the panel, Disconnect here, or unplugging USB all return it to the gauges
with the bus restored exactly as it boots (Stop on the panel disconnects
the app too, showing *Stopped on dash*; unplugging shows *Device
disconnected*). Nothing is persisted: a reboot
always comes up as a normal dash.

## Running

```sh
.venv/bin/python run.py               # GUI, auto-detect port
.venv/bin/python run.py --port /dev/tty.usbmodemXXXX
.venv/bin/python run.py --demo        # GUI on synthesized traffic, no hardware
.venv/bin/python run.py --selftest    # headless engine check (needs only pyserial)
.venv/bin/python run.py --fit 43      # fit a signal's stored points, search its payload
.venv/bin/python run.py --summary captures/<file>.csv  # what a dongle did to each module
.venv/bin/python run.py --active      # open the bus in NORMAL mode (can TX) — see below
```

`--demo` synthesizes realistic OBDEleven-style scan traffic (including an
unknown DID and a multi-frame response) so you can explore the whole UI with
nothing plugged in.

## The registry: active, internal and WIP

Every signal ever defined lives in one file, `signals.json`, tagged with a
`status`:

- **Active** — compiled into the firmware and offered on the dash. Shows up
  in the Signals watchlist picker, and the live engine decodes its DID
  automatically wherever it appears in Traffic.
- **Internal** — compiled into the firmware for its own use, never offered
  as a panel or on the phone page (Dim Ctrl, which drives Auto brightness).
  Everything else is as for Active: watchlist, live decode, Fake Car, and
  the same firmware checks on save.
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

Reordering, promoting and demoting are all safe for dashes that already
hold saved settings: each saved per-signal record carries its signal id, and
the dash matches them up again on its next boot (see
[core/config/README.md](../../core/config/README.md)). The dash keeps
settings for up to 64 active signals; `gen_catalog.py` refuses more.

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
- **Registry** — every signal, whatever its status, in one table — the
  birds-eye view for reviewing what's actually validated. Filter (All/Active
  only/Internal only/WIP only), **Set Status** (Active/Internal/WIP) for a
  fast bulk change without opening the editor, **Edit…** for anything else, **New Signal…** to hand-enter one you
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

Its right half is for working out the decode:

- **Payload bytes** — the newest payload as byte cells; bytes that changed
  between samples are tinted amber. Click a byte, or drag across up to four,
  to choose which bytes this signal reads (with Signed, and Bits from/count
  for a flag or nibble inside a byte). **Auto** goes back to the legacy
  decode (u8, or s16 from bytes 0–1). A pick that reads exactly what Auto
  reads is saved as Auto, which the firmware already compiles.
- **Live** — while the DID is answering, its current payload and what it
  decodes to right now.
- **Points** — raw → true-value pairs kept with the signal. **Add from Live**
  freezes the payload on the bus as a new row and puts you in its Value cell
  to type what the phone shows; **Add Row** is for typing one in. Values go
  in the signal's metric unit (or a label, for an Enum). Each row shows its
  raw value, what the current offset/scale makes of it, and the error, green
  when it's within the precision you wrote the value down at.
- **Fit** works out offset/scale from every point (or, for an Enum, the value
  → label table) and says how well it fits; if the points aren't a straight
  line it asks before using the closest fit. **Find Field…** tries every byte
  range in the payload, in both byte orders and with a few flag bits masked
  off the top or bottom (a 14-bit value under two status bits, say), and
  lists the best matches to pick from. Neither
  changes anything until you Save.

**New Field on DID…** (Registry and WIP tabs) starts a new signal on the
selected signal's DID and module, for a DID that packs several values into
one response: pick the other bytes, and the dash still sends one request
for all of them. The editor lists which bytes other signals already read.

The editor also sets the signal's **ECU** — the module the DID belongs to
(Engine, Transmission, or Custom tx/rx CAN ids). Learn and WIP pre-fill it
from the traffic when the DID has been seen answered, and the Learn, WIP and
Registry tables all show it. Learn lists a DID once per module that answers
it, since the same DID number means different things in different modules.
While connected to the car, a WIP signal with no ECU yet gets it filled in
(and `signals.json` saved) as soon as its DID is answered by exactly one
module; a DID answered by several is left for the editor, which asks which
one. Demo traffic never fills anything in. Learn keeps each DID's distinct
payloads (up to 50, with how often each was seen) rather than the last few
repeats, and saving a WIP signal stores those. Choosing **Unit: Enum** swaps the display range
for a value → label table (e.g. 0 = 1st, 8 = Shifting); the Signals tab and
Traffic log then show the label. Saving a signal **Active** or **Internal**
first checks it against what the firmware can compile (`tools/gen_catalog.py`)
alongside the other compiled signals: it needs an ECU, an Enum's labels have to fit the dash
(15 characters, plain ASCII), and no two modules may share a request or
response id.

## Recording and replaying

**● Rec** in the top bar (enabled while connected) writes every raw frame
from then on to `tools/monitor/captures/<YYYY-MM-DD_HHMMSS>.csv` — gitignored,
since drives stay local. The button shows the frame count while recording;
click it again, or Disconnect, to stop. Captures of demo traffic get a
`_demo` suffix.

A capture is the same CSV as Traffic's raw **Save Log…**, plus an `epoch_s`
wall-clock column (to line it up with a phone screen recording) and a `#`
header line naming the source. **Source → Replay capture…** then Connect
plays one back — or a raw Save Log — through the whole app in **real time**
or **all at once**: Signals, Traffic, Learn and the editor's live line all
see it as if it were the bus. Replaying clears the traffic seen so far.
Replaying a capture from the car can fill in WIP signals' ECUs just like
the live bus; a demo capture never does.

## Fake car

The **Fake Car** tab answers the OBDEleven dongle as if the Mac were the car,
so you choose exactly what a signal sends and read off what the phone makes
of it, with no timing to line up. The dongle plugs into a **bench** OBD
socket wired to the dash or sniffer's CAN bus (12 V on pin 16, ground on 4/5,
CAN-H on 6, CAN-L on 14, 120 Ω at each end) — never into the car, where the
real modules would answer too. Connect with **Listen-only unchecked** (the
fake car has to transmit), then **Start Fake Car**. It needs the Teensy dash
or the standalone Teensy sniffer (the S3 dash has no sniffer mode).

- Every single-request registry signal with a module is listed, and its
  module answers. **Filter** narrows the list by name, id, module or DID.
  A signal the recording answered is answered from it (as of **Answer as
  at**); any other gets a stand-in payload (a stored sample, else zeros).
  Changing a signal's value fakes it — its row turns blue — and **Reset**
  hands it back to the recording.
- Set what it sends by **Value** (in its units), **Raw** field value, **State**
  (an Enum's label), or the **Payload** bytes directly. **Should show** is what
  the phone ought to display if the signal's offset/scale are right.
- **Sweep raw** steps the raw value from/to/step, by hand (◀ Step / Step ▶)
  or **Auto every** few seconds.
- Type what the phone shows and press Enter (or **Add Point**) to save it,
  with the payload being sent, as a point on the signal (source `emulator`).
  **Points** lists every point on the signal, and below it the best field and
  scale/offset (or state mapping) the search finds for them, green once three
  points agree. **Use This Fit** saves it on the signal — no trip to the
  Registry needed.
- **Load Recording…** takes a capture of the dongle talking to the real car
  (● Rec while connecting OBDEleven to it). Everything not being faked —
  the connect handshake, identification, sessions, other DIDs — is answered
  exactly as the car answered, as of the **Answer as at** moment. Without a
  recording only the faked DIDs, tester present and session control are
  answered, which may not be enough for the app to connect. Frames the car
  sends on its own throughout the recording (the gateway's 0x17F00010
  heartbeat) are repeated at their recorded rate while the fake car runs; the
  recording line shows which. Only modules the dongle talked to in the
  recording exist — open every module you'll want on the bench while
  recording.
- **What the dongle asked** lists every request and how it was answered:
  anything amber is something the fake car couldn't answer.

**Source → Simulated dongle** tries all of it with no hardware: a pretend
dongle asks for each faked DID in turn, and the answers show up in Signals,
Traffic and Learn like the real bus. Whatever the fake car sends never fills
in a WIP signal's ECU.

## Passive vs. active

By default everything is **passive** — the app only listens. `--active` opens
the bus in normal mode so a later probing feature can transmit its own `0x22`
requests to sweep for DIDs. Only use it when you intend to participate on the
bus; passive monitoring between the OBDEleven and the car needs listen-only.

## Layout

| File | Role |
|------|------|
| `run.py` | entry point / CLI |
| `mqb_monitor/catalog.py` | loads/saves the `signals.json` registry; **firmware-accurate** decode; compiled-only (active + internal) DID index |
| `mqb_monitor/uds.py` | ISO-TP reassembly + UDS message parsing |
| `mqb_monitor/sources.py` | SLCAN serial source, hardware-free demo source, capture replay |
| `mqb_monitor/emulator.py` | the fake car: recording → answers, overrides, ISO-TP both ways, simulated dongle |
| `mqb_monitor/fake_car_tab.py` | the Fake Car tab |
| `mqb_monitor/capture.py` | the capture file format: writer (Record) and reader (replay) |
| `mqb_monitor/decode_panel.py` | the signal editor's decode side: byte strip, live payload, points table, Fit / Find Field |
| `mqb_monitor/solver.py` | fits offset/scale (or an enum table) to a signal's stored points; searches a payload for the field they match |
| `mqb_monitor/session.py` | the engine: frames → decode → signal/unknown state (thread-safe, GUI-agnostic) |
| `mqb_monitor/theme.py` | dark/blue-accent Fusion theme (palette + QSS) |
| `mqb_monitor/gui.py` | the PySide6 window: sidebar shell + five tabs + the signal editor dialog |

The engine (`session.py`) is deliberately GUI-free and is what `--selftest`
exercises, so decode/ISO-TP/registry logic can be tested without a display.
