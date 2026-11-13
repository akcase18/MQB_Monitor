# tools/

Developer tooling for the MQB dashboard project — host-side programs that
support firmware development but don't ship on any board.

## Contents

- **[`gen_catalog.py`](gen_catalog.py)** — compiles the firmware's signal
  catalog from the registry. The single source of truth is
  [`core/vehicle/data/signals.json`](../core/vehicle/data/signals.json) — a
  **two-stage registry**: every signal ever defined lives there, tagged
  `"status": "active"`, `"status": "internal"` (compiled in for the
  firmware's own use, e.g. Dim Ctrl for Auto brightness, but never offered
  on the dash or the phone page) or `"status": "wip"` (unconfirmed
  offset/scale, a placeholder name, etc.). This script compiles the active
  entries, then the internal ones, into
  `core/vehicle/src/signal_catalog.generated.inc` (the `SIGNAL_CATALOG[]`
  array `signal_catalog.cpp` includes) **and** keeps `SIGNAL_CATALOG_COUNT`
  and `INTERNAL_SIGNAL_COUNT` in `signal_catalog.hpp` in sync. WIP entries
  never reach the firmware. Run it after editing
  `signals.json`; commit the JSON and the generated file together.

  ```sh
  python3 tools/gen_catalog.py          # regenerate
  python3 tools/gen_catalog.py --check  # CI guard: fail if the .inc/count are stale
  ```

- **[`gen_value_fonts.py`](gen_value_fonts.py)** — regenerates the panels'
  bold value fonts (`gui/assets/fonts/jetbrains_mono_bold_{30..128}.c`) with
  digits plus only the characters active Enum labels use, so a label like
  `N` or `Closed` draws as big as a number. `gen_catalog.py` warns when a
  label needs a character they lack; until this is re-run, that label draws
  at the 24 px text size. Needs Node.js (`npx lv_font_conv`) and JetBrains
  Mono Bold **v2.002** (the release the fonts were cut from; later ones
  differ slightly):

  ```sh
  python3 tools/gen_value_fonts.py --font path/to/JetBrainsMono-Bold.ttf
  ```

- **[`fw_version.py`](fw_version.py)** — PlatformIO pre-build script for the
  release hardware, the `env:dash_s3` family, `env:teensymm` and
  `env:esp32c3`. It writes a
  generated `fw_version.h` with the version (`git describe --tags --always
  --dirty`; tag releases to get readable versions), which both boards show
  on the Settings screen. On the dash it also holds what the firmware update
  descriptor is checked against (see
  [platform/dash_s3](../platform/dash_s3/README.md#firmware-updates)): a CRC
  of the partition table, which the dash refuses OTA images over, and the
  LittleFS partition's size.

- **[`release.py`](release.py)** — builds a firmware release for the
  release hardware only, `env:dash_s3` (the S3 dash PCB) and the Teensy
  dash (`env:teensymm` plus its C3 relay, `env:esp32c3`), into
  `releases/<tag>/`: the S3 dash's OTA file, its USB install files and
  script, the Teensy's `.hex`, the C3's USB install, and a manifest that
  says whether the S3 dash can take it over the air. See
  [platform/dash_s3](../platform/dash_s3/README.md#releases).

  ```sh
  git tag v1.2.0 && python3 tools/release.py
  python3 tools/release.py --dev   # trial run from an untagged tree
  ```

- **[`partition_table.py`](partition_table.py)** — reads the ESP32 partition
  CSV and computes its layout CRC, for both scripts above.

- **[`monitor/`](monitor/)** — the **MQB Monitor**, a Mac (PySide6) app that
  passively watches UDS traffic between an OBDEleven and the car via a USB
  SLCAN sniffer (a standalone Teensy 4.0, or the dash itself in sniffer
  mode), decodes it against the *same* `signals.json` registry the
  firmware uses, and is how you add, review, and promote/demote signals — its
  Registry/Learn/WIP tabs read and write the registry directly. See its
  [README](monitor/README.md).

- **[`log_export/`](log_export/)** — pulls the dashboard firmware's
  black-box signal log (see [core/logging](../core/logging/README.md)) over
  USB serial and decodes it to CSV. A separate board/firmware/protocol from
  `monitor/`'s SLCAN sniffer above — talks to the dashboard firmware
  (`env:teensymm`) itself, not a standalone sniffer. Two front ends over one
  decoder (`mqb_logs/`): **`run.py`**, the **MQB Log Export** app — the same
  PySide6 shell and theme as `monitor/`, with a Device page (status,
  download, erase), a Logs page (sessions, preview, CSV export) and a Plot
  page (one session's signals against time, zoomable) — and
  **`dump_log.py`**, the scriptable CLI the VS Code tasks run. Either can
  read a storage *directory* instead of a device (`sim_storage/`), so the
  whole path works with nothing plugged in. See its
  [README](log_export/README.md).

- **[`bench/`](bench/)** — runs and analyzes the throughput tests in
  [THROUGHPUT_TEST_PLAN.md](../THROUGHPUT_TEST_PLAN.md): the v4 dash in
  bench mode fakes the car for the v5 dash, or fakes the dash for the car,
  and streams every frame with a microsecond timestamp; `run.py` turns the
  log into each module's answered/s and the requester's and responder's
  share of every cycle. See its [README](bench/README.md).

## The two-stage registry loop

```
                 core/vehicle/data/signals.json   ← every signal, any status
                    │                        │
      gen_catalog.py│                        │Catalog.load()
  (active+internal) │                        │
                    ▼                        ▼
   signal_catalog.generated.inc        tools/monitor (Mac app)
   + the counts (hpp)                   · decodes live traffic (active+internal)
                    │                    · Learn: discover unknown DIDs
   firmware build ◄─┘                    · Registry/WIP: review, promote/
                    ▲                        demote, edit -- writes back to
                    │                        signals.json
                    └──────────────  re-run gen_catalog.py  ◄─────────────┘
                       (or the Registry tab's Regenerate button)
```

The monitor's decode logic (`mqb_monitor/catalog.py`) mirrors the firmware's
(`core/can` + `core/vehicle`) byte-for-byte — same 1-byte-unsigned /
2-byte-signed payload rule, same `(raw + offset) * scale`, same transforms —
so a value shown in the tool matches what the dashboard would show. The same
mirroring applies to the active/WIP split: `Catalog.known_dids()` /
`signals_for_did()` only ever resolve **active** signals, so the live monitor
treats a WIP signal's DID as "unknown, log it raw" exactly like the firmware
would if it saw that DID — WIP status is a monitor-tool-only concept until
promoted.
