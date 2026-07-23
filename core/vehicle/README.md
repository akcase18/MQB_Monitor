# core/vehicle/

The vehicle-signal data model: a static, ROM-able catalog of every signal the dashboard can display, a runtime value store fed by decoded UDS responses, and signal-scoped warning-threshold evaluation.

## Files

- **`data/signals.json`** — the **canonical** catalog: a plain-data file that is the single source of truth shared between this firmware and the Mac monitor tool ([tools/monitor](../../tools/README.md)). `src/signal_catalog.cpp`'s `SIGNAL_CATALOG[]` array is **generated** from it into `src/signal_catalog.generated.inc` by [`tools/gen_catalog.py`](../../tools/gen_catalog.py) — edit the JSON and re-run that script (commit both). Only the `transform_*` functions and unit helpers are hand-written in the `.cpp`; a signal's JSON `"transform"` field names one of them.

- **`inc/signal_catalog.hpp`** / **`src/signal_catalog.cpp`** — `SIGNAL_CATALOG`, the 22 signals (generated from `data/signals.json`, above), each a `SignalSpec`:
  - `id`, `name`, `unit_type` (`UnitType::{None,Degrees,Percent,Pressure,RPM,Speed,Temp,Torque,Volume}`), `cycle_time_ms` (how often it's polled).
  - `requests[]`/`num_requests` — up to 2 `UdsRequestSpec`s (see [core/can](../can/README.md)) whose decoded values feed this signal.
  - `transform` — combines those decoded, already offset/scaled request values into the signal's final value (most signals are a straight passthrough; a couple, like actual intake pressure, are a difference of two requests).
  - `display_min`/`display_max` — full-scale bounds in the signal's native (metric) unit. These are sensible engineering defaults (e.g. coolant 0–250°F equivalent, boost -15–25psi), **not** per-vehicle-tuned — they scale a Bar/Gauge panel's fill and bound the warning-threshold editor's +/- range. Real per-car tuning is a planned future phone-app feature, not built here (see [TODO.md](../../TODO.md)).

  Also: `signal_catalog_find(id)`, unit-label/decimal-precision helpers, and metric↔imperial conversion (`unit_convert_to_imperial`/`unit_convert_to_metric`) used both for display and for converting warning thresholds edited in whichever unit system a panel is currently showing.

  The current catalog: Coolant Temp, Engine Oil Temp, Intake Air Temp, Ambient Pressure, Charge Pressure, Intake Pressure (Actual), Intake Pressure (Specified), Engine Torque, Engine Speed (RPM), Throttle Position, Vehicle Speed, Fuel Level, per-cylinder (1–4) Misfire Count and Timing Correction, and a combined Misfire Count.

- **`inc/signal_store.hpp`** / **`src/signal_store.cpp`** — `SignalStore`, the runtime value cache:
  - `on_uds_response(byte1, byte2, raw, now_ms)` — the entry point fed by `UdsClient::poll()`. Finds every catalog request matching `(byte1, byte2)`, decodes it, and once *all* of a signal's requests have reported for this cycle, runs its `transform` and records the result.
  - `get(id)` / `get_display_value(id, is_metric)` — read a signal's current `SignalValue` (raw value, `has_data` flag, last-update timestamp) or its unit-converted, precision-rounded display value.

- **`inc/warning.hpp`** / **`src/warning.cpp`** — `evaluate_warning(value, thresholds)`: compares a `SignalValue` against a `WarningThresholds{lo, hi, use_lo, use_hi}`, gated by each bound's own on/off toggle and by `has_data` (so a signal can't false-trigger a warning before its first response). Lives here rather than in [core/config](../config/README.md) so this stays ignorant of how/where thresholds are persisted — config just stores a `WarningThresholds` per signal and hands it here.

## Usage

`core::SIGNAL_CATALOG_COUNT` (22) sizes fixed arrays throughout the app — `SignalStore`'s value cache, `core::Config::signals[]` (see [core/config](../config/README.md)), and the `signal_names`/`signal_ids` tables `src/main.cpp` builds for the Configurator/Signal Picker screens. Adding a signal means adding one entry to `data/signals.json`, re-running `tools/gen_catalog.py`, and bumping `SIGNAL_CATALOG_COUNT` in `signal_catalog.hpp` to match — everything downstream sizes off that constant, and the generated `.inc`'s `static_assert` fails the build if the count is stale.
