# core/config/

The persisted configuration schema and its load/save/migrate logic, storage-agnostic over the platform's `IConfigStore` (EEPROM on Teensy, a file on the simulator — see [platform](../../platform/README.md)).

## Files

- **`inc/config.hpp`** — the schema:
  - `ConfigHeader { magic, version }` — versioning/validity check, written by `ConfigStore`.
  - `Settings { day_brightness, night_brightness, num_panels, orientation, logging_enabled, flipped_horizontal, flipped_vertical, open_orientation_on_boot, accent_color }` — global settings, edited from the Settings screen. `logging_enabled` gates [core/logging](../logging/README.md)'s `Logger` entirely and defaults **off**: capture is a deliberate "record this drive" action, not something that should burn flash write-cycles on every commute. It's toggled from `LoggingScreen` (swipe up from the dashboard), not from the Settings screen — see [gui/screens](../../gui/screens/README.md).
  - `PrioritizedSignal { signal_id, units, display_mode }`, one per priority rank (`MAX_PANELS = 6`) — which signal is the Nth most important, in which unit system, drawn how. Indexed by **priority rank** (0 = most important), not by screen slot — a signal's on-screen prominence follows its rank across every panel count, rather than being tied to whichever slot happened to be "panel 1". Resolve rank → slot via `gui::compute_prominence_ranks()` (see [gui/screens](../../gui/screens/README.md)) for the current panel count/orientation.
  - `SignalConfig { warn }` — one `WarningThresholds` (see [core/vehicle](../vehicle/README.md)) per catalog entry, indexed by catalog position. Warning thresholds are keyed **by signal, not by rank** — so reassigning a signal to a different rank carries its thresholds with it automatically, instead of needing to be re-entered.
  - `Config { header, settings, priorities[MAX_PANELS], signals[SIGNAL_CATALOG_COUNT] }` — the whole persisted blob.
  
  `MAX_PANELS` (6) is a fixed compile-time capacity; `Settings::num_panels` is the *active* count within it (1–6) — unused ranks are simply inactive, not compiled out.

- **`inc/config_store.hpp`** / **`src/config_store.cpp`** — `ConfigStore`:
  - `MAGIC_NUMBER` / `CURRENT_VERSION` (currently 6: v2 added `PanelConfig::display_mode`, v3 added `Settings::accent_color`, v4 added `SignalConfig`'s custom-range fields, v5 renamed `Config.panels[]` (screen-slot-indexed) to `Config.priorities[]` (priority-rank-indexed), v6 added `Settings::logging_enabled`) gate loading.
  - `load()` — unrecognized magic → `set_defaults()` (and persists them); older version → `migrate()` (and persists the result); current version → full load; newer version (firmware this board has rolled back from, see [platform/dash_s3/OTA_PLAN.md](../../platform/dash_s3/OTA_PLAN.md)) → defaults, **not** persisted, so reinstalling the newer firmware finds its config again — until something here saves.
  - `save()` — writes the entire `Config` back.
  - `save_priority(rank)` / `save_signal(signal_id)` — partial writes, so e.g. reassigning one rank or editing one signal's thresholds doesn't rewrite the whole blob. Over a newer firmware's config they do a full `save()` instead: one record written at this layout's offset, under a header still naming the newer version, would load as valid there.
  - `set_defaults()` — day/night brightness 100%/20%, 5 active panels (rank 0 = Coolant Temp down through rank 4 = Intake Pressure Actual, with rank 5 = Engine Torque inactive by default), all warning thresholds off, green accent, logging off.
  - `migrate(old_version)` — versions before 6 never left the bench, so they fall through to `set_defaults()`. Dashes in the field hold v6 settings, so the next layout change needs a real step here that converts v6. A `static_assert` in `config_store.cpp` pins `sizeof(Config)` and its key offsets to `CURRENT_VERSION`, so a changed layout — including a signal added to the catalog, which grows `signals[]` — doesn't compile until the version is bumped and those numbers updated, which is the prompt to write that step.

## Usage

`src/main.cpp` owns the one `ConfigStore` instance (inside its `Composition` singleton), calls `load()` once at startup, and passes it into `gui::VehicleBinding` (see [gui/bindings](../../gui/bindings/README.md)), which is the only code that reads/writes `Config` fields on behalf of the UI.
