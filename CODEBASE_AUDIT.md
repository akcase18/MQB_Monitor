# Codebase Audit

Audit of the two-product source tree as of `7ce0cb2` on `feat/spi_display`, run against
the actual source rather than the documentation. Written to be handed to someone who
has not worked in this repo before.

Scope: the whole tree except `lib/` (vendored third-party) and `tools/monitor/.venv`.

**Contents**

- [Baseline](#baseline)
- [Verdict](#verdict)
- [Findings](#findings)
- [Where the ESP32-S3 lands](#where-the-esp32-s3-lands)
- [Plan](#plan)
- [What not to change](#what-not-to-change)
- [How this was verified](#how-this-was-verified)

---

## Baseline

Everything below was measured, not estimated.

| | |
|---|---|
| Environments building clean | 3 / 3 (`teensymm`, `simulator`, `simulator_gauge`) |
| Host tests passing | 154 / 154 (`pio test -e simulator`) |
| Layering violations found | 0 |
| Firmware source | 21,803 lines (`core/` + `gui/` + `src/` + `platform/` + `remote/`) |
| Test source | 6,160 lines |
| Generated catalogs | 456 lines |
| Generated fonts | 10,028 lines |
| Teensy image | FLASH code 349,480 B, data 148,012 B; RAM1 vars 116,192 B; RAM2 vars 15,488 B |

Build warnings: ten, all of them the LVGL 9 deprecation noise already recorded in
[TODO.md](TODO.md). Nothing else.

---

## Verdict

**Bloat is not the problem.** Excluding vendored libraries, generated fonts and tests,
the whole thing is 21,803 lines of firmware covering two products, seven screens, a UDS
protocol stack, an analog sensor stack, a logging ring, a wire protocol and a web config
form. That is lean for the feature set. Every function declared in a public header was
extracted and reference-counted across the tree — exactly **two** are dead, about 50
lines combined. There is no fat here worth a refactor.

**Cross-contamination between the two products is not the problem either — today.** The
dependency rule in [ARCHITECTURE.md](ARCHITECTURE.md) was checked against the real
`#include` graph rather than taken on trust, and it holds completely:

- `core/` contains no LVGL, Arduino or SDL include.
- `gui/widgets` and `gui/screens` include nothing from `core/`, `remote/` or `platform/`.
- `core/vehicle` includes nothing from `core/can`.
- There is no `#ifdef PLATFORM_*` anywhere outside `platform/`.
- Product selection lives entirely in `platformio.ini`.

This is unusually disciplined and it is the reason gauge development has not broken the
Teensy product.

**What will bite you is the seam that discipline does not cover: persisted state, and the
board layer that does not exist yet.** Both products write a config blob with the same
magic number and the same version byte but a *different struct size*, which has already
caused one visible failure. And the gauge product has no hardware target at all, so its
hardest integration work is still ahead with nowhere to land.

---

## Findings

Ten findings, ranked. Two are live bugs; the rest are structural or hygiene.

### B1 — A newer config blob is loaded raw instead of rejected

**Severity: bug.** [core/config/src/config_store.cpp:51](core/config/src/config_store.cpp#L51)

`ConfigStore::load()` handles two cases and silently falls through on a third. Only a
*lower* version triggers `migrate()`; a blob written by newer firmware passes straight
into a raw read at the current struct size.

```cpp
if (header.magic != MAGIC_NUMBER) { set_defaults(); save(); return; }

if (header.version < CURRENT_VERSION) { migrate(header.version); save(); return; }

backing_.read(0, &config_, sizeof(config_));   // version > CURRENT_VERSION lands here
```

`test_unrecognized_version_resets_to_defaults()` in
[test/test_core_config/test_main.cpp:199](test/test_core_config/test_main.cpp#L199)
covers version `0` (older) and a bad magic. The downgrade path — flashing older firmware
onto a device that has run newer — is neither handled nor tested.

**Fix.** Change the guard to `header.version != CURRENT_VERSION` so both drift directions
route through `migrate()`, and add the mirror test with a version above current.

### B2 — The config format cannot tell the two products apart

**Severity: bug.** [core/config/inc/config.hpp:92](core/config/inc/config.hpp#L92),
[core/config/inc/config_store.hpp:20](core/config/inc/config_store.hpp#L20)

`Config` embeds a per-product array:

```cpp
struct Config
{
        ConfigHeader      header;
        Settings          settings;
        PrioritizedSignal priorities[MAX_PANELS];
        SignalConfig      signals[SIGNAL_CATALOG_COUNT];   // 22 for MQB, 3 for gauge
};
```

So `sizeof(Config)` differs between products. But `MAGIC_NUMBER` (`0xFEA7`) and
`CURRENT_VERSION` (`6`) are shared constants with no product discriminator. A blob from
either product validates cleanly against the other and is then read at the wrong size.

This is not hypothetical. It already happened, and [.gitignore](.gitignore) records it:

> The gauge simulator's config blob. There is one per product now (both write a
> `core::Config`, but the struct's size and every signal id in it come from that product's
> own catalog — sharing one file made the gauge simulator load the MQB panel assignment
> and blank the panels whose ids it does not have).

The response was to split the *file* (`sim_config_gauge.bin`). That fixes the simulator
but leaves the *format* just as ambiguous for the next occurrence — an ESP32-S3 reusing a
flash region, or config arriving over the phone form.

**Fix.** Make the blob self-describing: add a product/catalog fingerprint and a
`uint16_t struct_size` to `ConfigHeader`, and reject on mismatch exactly the way a bad
magic is rejected. Bump `CURRENT_VERSION` to 7. This is cheap right now, and
`migrate()`'s own comment says why:

> No device has shipped with saved config yet, so every version bump so far has been a
> reset-to-defaults.

That stops being true the moment a unit goes out.

### A1 — The ESP32-S3 has no board layer and no build target

**Severity: structural gap.** `platform/`, `platformio.ini`

The gauge product exists only as `env:simulator_gauge` — SDL2 on a desktop. The only
implementations of `IAnalogInput` and `IButtons` anywhere in the tree are:

```
platform/null_analog_input.hpp          NullAnalogInput
platform/null_buttons.hpp               NullButtons
platform/simulator/inc/simulator_analog_input.hpp   SimulatorAnalogInput
platform/simulator/inc/simulator_buttons.hpp        SimulatorButtons
```

There is no `platform/esp32s3/`. The existing `platform/esp32/` is the **ESP32-C3 Wi-Fi
relay for the MQB product** — a different chip doing a different job (see A4).

**Why it matters.** This is the honest shape of the project: the gauge's *logic* is real
and tested, but its hardest integration — ADC sampling, button GPIO, SPI panel bring-up,
flash-backed config, on-die Wi-Fi — is entirely ahead. The abstraction is ready to receive
it; nothing has landed yet.

### A2 — 88 identical lines across the two composition roots

**Severity: duplication, not yet drifted.** `src/main.cpp` (540 lines),
`src/main_gauge.cpp` (593 lines)

Matching the two files line by line after stripping comments and blanks, 88 lines are
byte-identical in runs of four or more:

| Duplicated block | Lines | Depends only on |
|---|---|---|
| `sync_active_signals()` + its scheduler task | 19 | `ConfigStore`, `ISignalSource` |
| Accent-color validation at boot | 15 | `Settings`, `ACCENT_PRESETS` |
| `composition()` accessor | 9 | — |
| `logging_sample_task()`, `refresh_info_screen_task()` | 9 | `Logger`, `InfoScreen` |
| Storage bring-up, screen load, include block | ~36 | mixed |

None of it has drifted yet. The risk is specific: `sync_active_signals()` is the
config-to-acquisition bridge, and a silent divergence there means one product quietly
polls the wrong set of signals — with no build error and no test failure.

**Fix.** These are pure functions of `core::` types with no `gui::` or product knowledge,
which is exactly what `src/app/` already exists to hold. Lift them as free functions
taking explicit references:

```cpp
namespace app { void sync_active_signals(core::ConfigStore &, core::ISignalSource &); }
```

### A3 — The phone config form is shaped around a wire the S3 will not have

**Severity: structural.**
[platform/esp32/inc/config_page_render.hpp:37](platform/esp32/inc/config_page_render.hpp#L37)

```cpp
void render_config_page(
    const remote::protocol::SettingsWire           &settings,
    const remote::protocol::PanelWire              *panels,
    uint8_t                                         panel_count,
    const remote::protocol::SignalCatalogEntryWire *catalog,
    const remote::protocol::SignalConfigWire       *signal_cfg,
    ...);
```

Correct for the C3, which only ever sees decoded wire records. But the ESP32-S3 has Wi-Fi
on-die and direct access to `core::Config` — there is no wire. Reusing this function there
means fabricating `remote::protocol::*Wire` structs from local config purely to satisfy
the signature, which quietly promotes a transport format into a view-model.

**Fix.** Before writing S3 Wi-Fi, re-express the parameters as a small neutral view-model
struct owned by the form. The C3 fills it from wire records; the S3 fills it from
`core::Config` directly. One renderer, two adapters, neither product bent around the
other's transport.

### A4 — `platform/esp32/` is not a platform

**Severity: naming, about to become confusing.** `platform/esp32/`

Its own README says so:

> Unlike `platform/teensy`/`platform/simulator`, this is **not** another implementation of
> `platform.hpp`'s `Platform` interface — the C3 has no display, touch, or CAN, so it
> isn't part of that abstraction at all.

It is a standalone product firmware with its own `main.cpp` that includes
`remote_protocol.hpp` — a dependency no board HAL has. Every other directory in the tree
is named for its layer; this one is named for its chip and sits in the wrong layer.

**Why it matters.** It costs nothing today, but when `platform/esp32s3/` arrives as a
genuine `Platform` implementation, the tree will hold two ESP32 directories meaning
entirely different things. Renaming now is a `git mv` plus four `platformio.ini` lines —
candidates: `firmware/wifi_relay/`, `remote/relay_c3/`.

### A5 — Three mirrored tables, enforced only by a comment

**Severity: correct today, unguarded.**
[platform/esp32/inc/unit_labels.hpp](platform/esp32/inc/unit_labels.hpp)

The C3 deliberately duplicates `unit_label()`, both unit conversions, and all nine
`ACCENT_PRESETS` rather than linking `core::vehicle`/`gui::theme` into that env. Each was
diffed against its source and **all are currently correct**, down to the conversion
constants (`14.5038f`, `0.7375621493f`, `1.609f`, `3.785f`) and every preset hex value.

The header additionally assumes `esp32::WireUnitType`'s ordinals match `core::UnitType`'s
exactly — they do, `None=0` through `Volume=8` — but nothing enforces that. Reordering the
`core` enum would mislabel every unit on the phone form with no build error anywhere.

**Fix.** The duplication itself is justified; do not undo it. But `env:web_preview`
already compiles `core/vehicle/src` *and* this header into one binary, so it can carry
`static_assert`s pinning the ordinals and the preset table at no runtime cost. Turn the
comment into a compile error.

### H1 — Two dead functions, the entire dead-code inventory

**Severity: hygiene.** [core/sensors/src/curve_library.cpp:215](core/sensors/src/curve_library.cpp#L215),
[core/sensors/src/conditioning.cpp:70](core/sensors/src/conditioning.cpp#L70)

All 212 function names declared across `core/`, `gui/` and `remote/` public headers were
extracted and reference-counted tree-wide, including tests. Only two have no caller and no
test:

- `core::sensor_cal_default(SensorKind)` — ~25 lines
- `core::sensor_status_text(SensorStatus)` — ~22 lines

**Recommendation.** Keep `sensor_status_text()`: it is the obvious source for a sensor
diagnostics readout on the gauge, and it is nine trivial cases. Delete
`sensor_cal_default()` — `sensor_preset_apply()` is what `src/main_gauge.cpp` actually
calls, so it is a second unused way to do the same job.

### H2 — Two build artifacts tracked in git

**Severity: hygiene.** `config_preview.html`, `sim_config.bin`

- `config_preview.html` — 30 KB of generated output from `env:web_preview`, committed once
  at `f783b1d` and never regenerated.
- `sim_config.bin` — runtime state the simulator rewrites on every run, so it dirties the
  working tree as a side effect of running the app.

[.gitignore](.gitignore) already flags the second one:

> `sim_config.bin` is *tracked*, which predates this; this one deliberately is not, so a
> simulator run does not dirty the tree. Flip either way, but flip both.

**Fix.** Take the flip. `git rm --cached` both and ignore them, matching how
`sim_config_gauge.bin` and `log_export_out.csv` are already handled.

---

## Where the ESP32-S3 lands

The layering already holds a board-shaped hole. Everything above the dashed row exists and
is tested; the dashed row is the whole of what the S3 needs.

```
  src/main_gauge.cpp          composition root - exists, 593 lines, runs today
          |
  gui/ + core/                screens, sensors, config, logging - shared, 154 tests
          |
  platform/platform.hpp       IDisplay IAnalogInput IButtons IConfigStore IClock IStorage
          |
  platform/esp32s3/  <-- DOES NOT EXIST
                              ADC, GPIO buttons, SPI panel, NVS/LittleFS, on-die Wi-Fi
```

Only `platform/simulator/` fills that row today. Nothing above it needs to change to accept
a real board — which is the payoff of the discipline already in place, and the reason this
gap is a task rather than a problem.

---

## Plan

Ordered so each tier is independently shippable and nothing blocks gauge development.
**Tier 1 is the only part that touches the Teensy, and it is the part that protects it.**

### Tier 1 — Close the two live bugs (do first)

Both sit in `core/config`. Both are cheap right now specifically because nothing has
shipped with saved config, and both get more expensive with every device in the field.

1. **Reject newer config blobs.** `header.version < CURRENT_VERSION` becomes `!=`, plus a
   downgrade test mirroring the existing older-version one. (B1)
2. **Make the blob self-describing.** Add a product fingerprint and `struct_size` to
   `ConfigHeader`; reject on mismatch; bump `CURRENT_VERSION` to 7. This is the structural
   fix for the failure already patched by splitting files. (B2)
3. **Resolve the touch-driver note.** Either validate the FT6236 on hardware or correct
   [TODO.md](TODO.md) to say the active driver is unvalidated. (S1)

### Tier 2 — Delete and tidy (one sitting)

The entire removal list. It is short because the codebase is not bloated.

- **Delete `sensor_cal_default()`** — a duplicate path to what `sensor_preset_apply()`
  already does. Keep `sensor_status_text()`. (H1)
- **Untrack `config_preview.html` and `sim_config.bin`**, and ignore both. (H2)
- **Sweep the LVGL 9 deprecations** — `lv_obj_add_flag`/`remove_flag`/`has_flag` across
  `info_screen.cpp` and `toast_overlay.cpp`. Ten warnings, already on the TODO, and the
  only warning noise in an otherwise clean build. Do it in one pass so `gui/` does not end
  up with two idioms.

### Tier 3 — Harden the seams before the S3 arrives (before Wi-Fi work)

Each is cheap now and awkward once S3 code depends on the current shape.

1. **Lift the 88 duplicated lines into `src/app/`.** Start with `sync_active_signals()` —
   the one whose divergence would be silent and would misroute polling. (A2)
2. **Rename `platform/esp32/`** to something naming its role rather than its chip, before a
   second ESP32 directory exists. (A4)
3. **Add `static_assert`s to `env:web_preview`** pinning `WireUnitType`'s ordinals and the
   accent table to their `core`/`gui` sources. That env already compiles both sides. (A5)
4. **Re-shape `render_config_page()`** around a neutral view-model instead of wire structs,
   so the S3 can reuse it without inventing a transport. (A3)

### Tier 4 — Build the board layer (the real work)

With Tier 3 done, this is additive: a new directory and a new `[env:]`, no existing file
modified.

- **`platform/esp32s3/`** implementing `IDisplay`, `IAnalogInput`, `IButtons`,
  `IConfigStore`, `IClock`, `IStorage` — mirroring what `platform/simulator/` already
  satisfies.
- **`[env:esp32s3_gauge]`** pairing that directory with `src/main_gauge.cpp` and
  `core/vehicle/generated/gauge/`, following `env:simulator_gauge`'s `build_src_filter`
  exactly.
- **Keep `env:simulator_gauge` alive.** It is what makes gauge logic testable without
  hardware, and it is the reason this audit could verify gauge behaviour at all.

---

## What not to change

Stated explicitly, because an audit invites over-correction and several of these look like
duplication until you read why they exist.

- **Two composition roots.** Beyond the 88 shared lines they diverge completely — screen
  callbacks versus button actions. Merging them behind a flag would reintroduce exactly the
  product coupling the tree is built to avoid.
- **Two `Composition` structs.** They share five members and differ in twelve. A common
  base would trade real clarity for five lines.
- **Two signal registries.** Already litigated and settled correctly in [TODO.md](TODO.md)
  — the products share no signals, so one filtered registry would be a fiction.
- **The C3's duplicated unit tables.** Linking `core::vehicle` into the relay firmware to
  save three small tables would cost far more than it saves. Pin them with asserts instead
  (A5).
- **Both dormant Teensy backends** (`TeensySdCard`, and whichever touch driver is
  inactive). Each compiles in its own translation unit so it cannot bit-rot, and the
  reasoning is documented at the switch. That is the right way to keep an alternate.
- **`WarningLatch` on the gauge only.** Deliberate and documented — a null default keeps
  CAN behaviour unchanged. Worth *considering* for the Teensy later, since a brief
  overboost spike currently flashes and vanishes there, but that is a feature decision, not
  a defect.

---

## How this was verified

So a reviewer can reproduce or challenge any claim above.

| Claim | How it was checked |
|---|---|
| Zero layering violations | `grep` for `#include` of `core/`, `platform/`, `remote/` inside `gui/widgets`, `gui/screens`, `gui/theme`; for LVGL/Arduino/SDL inside `core/`; for `core/can` inside `core/vehicle`; for `PLATFORM_*` preprocessor branches outside `platform/` |
| 3 / 3 envs build | `pio run -e simulator`, `-e simulator_gauge` (after `-t clean`), `-e teensymm` |
| 154 / 154 tests | `pio test -e simulator` |
| Line counts | `git ls-files` filtered by extension and path, excluding `lib/` and `gui/assets/fonts` |
| Dead code | All 212 function names declared in `*/inc/*.hpp` extracted, then `grep -oF "<name>("` counted across every tracked `.cpp`/`.hpp` |
| 88 duplicated lines | `difflib.SequenceMatcher` over both roots with comments and blanks stripped, counting equal runs of >= 4 lines |
| Mirrored tables in sync | Side-by-side read of `platform/esp32/inc/unit_labels.hpp` against `core/vehicle/src/signal_catalog.cpp` and `gui/theme/src/theme.cpp` |
| Config size divergence | `SIGNAL_CATALOG_COUNT` read from both `core/vehicle/generated/{mqb,gauge}/signal_catalog_count.inc` (22 and 3) against `struct Config` |

One note for whoever runs the tests: `pio test -e simulator` overwrites the simulator's
build output, so run `pio run -e simulator` afterward to restore the interactive binary.

No code was changed in producing this audit.
