# tools/

Developer tooling for the MQB dashboard project — host-side programs that
support firmware development but don't ship on any board.

## Contents

- **[`gen_catalog.py`](gen_catalog.py)** — compiles each product's signal
  catalog from its registry.

  **This tree builds two products, and they share no signals at all** — an
  MQB car cannot report oil pressure, and the analog gauge has no bus to ask
  over. So there is one registry per product,
  [`core/vehicle/data/mqb/signals.json`](../core/vehicle/data/mqb/signals.json)
  and
  [`core/vehicle/data/gauge/signals.json`](../core/vehicle/data/gauge/signals.json),
  each generating into its own `core/vehicle/generated/<product>/`.
  `platformio.ini` puts exactly one of those directories on the include path
  per environment, which is the same "configuration picks the file"
  mechanism `build_src_filter` already uses to pick a board — so there is no
  `#ifdef` anywhere in `core/`.

  Each is a **two-stage registry**: every signal ever defined lives there,
  tagged `"status": "active"` or `"status": "wip"` (unconfirmed offset/scale,
  a placeholder name, etc.). One filtered pass over the active entries emits:

  | file | what it is |
  |---|---|
  | `signal_catalog.generated.inc` | the `SIGNAL_CATALOG[]` array |
  | `signal_catalog_count.inc` | `SIGNAL_CATALOG_COUNT`, which sizes fixed arrays app-wide |
  | `signal_defaults.generated.inc` | the factory panel assignment `ConfigStore::set_defaults()` reads |
  | `uds_binding.generated.inc` + `_count.inc` | acquisition — bus products only, into `core/can/generated/<product>/` |

  The catalog and binding tables are **parallel arrays** (entry *i* of each
  is the same signal), which holds by construction because both come out of
  the same pass. The counts are their own tiny includes rather than
  constants patched into the headers: a hand-declared count cannot describe
  two products, and a header a script rewrites in place is a trap for
  whoever edits it next.

  WIP entries never reach the firmware. Run it after editing any registry;
  commit the JSON and the generated files together.

  ```sh
  python3 tools/gen_catalog.py                   # regenerate every product
  python3 tools/gen_catalog.py --product gauge   # just one
  python3 tools/gen_catalog.py --check           # CI guard: fail if anything is stale
  ```

- **[`monitor/`](monitor/)** — the **MQB Monitor**, a Mac (PySide6) app that
  passively watches UDS traffic between an OBDEleven and the car via a Teensy
  4.0 CAN sniffer, decodes it against the *same* `data/mqb/signals.json` registry the
  MQB firmware uses, and is how you add, review, and promote/demote signals — its
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

- **[`winid/`](winid/)** — screenshots the desktop simulator for visual
  verification of GUI work. macOS only.

  `winid.c` prints the CoreGraphics window IDs owned by a process;
  `shoot.sh` wraps the whole thing:

  ```sh
  tools/winid/shoot.sh shot.png                  # the MQB product
  tools/winid/shoot.sh gauge.png simulator_gauge # the analog gauge
  ```

  It builds the helper on first use, builds and relaunches that env's
  simulator if it is not running, and captures the window. The two
  simulators are separate binaries and can be up at the same time, which is
  the point: one command each proves both products still render.

  **Why not just `screencapture -R`.** A region capture takes whatever is
  composited at those coordinates **on the Space that is currently
  visible** — not the window you asked System Events for. With the
  simulator on a background Space (a long build running on one desktop
  while you work on another, or anything in fullscreen), System Events
  still reports a perfectly plausible position and size and the capture
  silently returns a *different application's* pixels. It does not fail. It
  returns the wrong image, which is worse. `screencapture -l <CGWindowID>`
  addresses the window itself: right regardless of Space, and it neither
  raises nor focuses anything, so it cannot disturb whatever is in front.
  macOS ships no CLI for that id and the system python3 has no Quartz
  bindings, which is the only reason there is C here at all.

  **Two filtering decisions, both learned by getting a wrong image.**

  The listing asks for `kCGWindowListOptionAll`, not
  `kCGWindowListOptionOnScreenOnly`. "On screen" means *on the Space that
  is currently visible*, so filtering by it reproduces the exact blind spot
  this tool exists to fix — a simulator on a background Space is missing
  from the listing even though `screencapture -l` captures it perfectly
  well. The first version used `OnScreenOnly` and appeared to work, because
  the window happened to be on the visible Space; it returned "no window"
  the first time it was pointed at one that was not.

  And it picks windows that have a **title**, not simply the biggest one.
  An SDL process owns several untitled siblings — shadow and chrome
  helpers, and on the MQB product a 500×500 one that is *larger than the
  panel itself*, so "largest wins" captures a blank white square. Only the
  real window carries a `kCGWindowName`; area is the tiebreak.

  Two traps the script already handles: `pio test` overwrites
  `.pio/build/<env>/program` with the Unity test runner, which exits
  instead of opening a window (so it always rebuilds first), and the
  compiled `winid` binary is `.gitignore`d rather than committed. Screen
  Recording permission is required for whichever terminal or editor runs
  the capture — it covers reading window titles too, so without it every
  name comes back empty and the tool finds nothing, which fails loudly
  rather than capturing the wrong thing.

## The two-stage registry loop

```
            core/vehicle/data/mqb/signals.json   ← every signal, active + WIP
                    │                        │
      gen_catalog.py│                        │Catalog.load()
      (active only) │                        │
                    ▼                        ▼
   generated/mqb/*.inc                 tools/monitor (Mac app)
   (catalog, count, defaults,           · decodes live traffic (active only)
    uds bindings)  │                    · Learn: discover unknown DIDs
   firmware build ◄─┘                    · Registry/WIP: review, promote/
                    ▲                        demote, edit -- writes back to
                    │                        signals.json
                    └──────────────  re-run gen_catalog.py  ◄─────────────┘
                       (or the Registry tab's Regenerate button)

The gauge product has the same left-hand half from
core/vehicle/data/gauge/signals.json, and no right-hand half at all — the
monitor tool learns UDS DIDs, and that product has no bus.
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
