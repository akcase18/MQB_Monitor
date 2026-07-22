# core/config/

The persisted configuration schema and its load/save/migrate logic, storage-agnostic over the platform's `IConfigStore` (EEPROM on Teensy, a file on the simulator — see [platform](../../platform/README.md)).

## Files

- **`inc/config.hpp`** — the schema:
  - `ConfigHeader { magic, version }` — versioning/validity check, written by `ConfigStore`.
  - `Settings { day_brightness, night_brightness, num_panels, orientation, accent_color }` — global settings, edited from the Settings screen.
  - `PrioritizedSignal { signal_id, units, display_mode }`, one per priority rank (`MAX_PANELS = 6`) — which signal is the Nth most important, in which unit system, drawn how. Indexed by **priority rank** (0 = most important), not by screen slot — a signal's on-screen prominence follows its rank across every panel count, rather than being tied to whichever slot happened to be "panel 1". Resolve rank → slot via `gui::compute_prominence_ranks()` (see [gui/screens](../../gui/screens/README.md)) for the current panel count/orientation.
  - `SignalConfig { warn }` — one `WarningThresholds` (see [core/vehicle](../vehicle/README.md)) per catalog entry, indexed by catalog position. Warning thresholds are keyed **by signal, not by rank** — so reassigning a signal to a different rank carries its thresholds with it automatically, instead of needing to be re-entered.
  - `Config { header, settings, priorities[MAX_PANELS], signals[SIGNAL_CATALOG_COUNT] }` — the whole persisted blob.
  
  `MAX_PANELS` (6) is a fixed compile-time capacity; `Settings::num_panels` is the *active* count within it (1–6) — unused ranks are simply inactive, not compiled out.

- **`inc/config_store.hpp`** / **`src/config_store.cpp`** — `ConfigStore`:
  - `MAGIC_NUMBER` / `CURRENT_VERSION` (currently 5: v2 added `PanelConfig::display_mode`, v3 added `Settings::accent_color`, v4 added `SignalConfig`'s custom-range fields, v5 renamed `Config.panels[]` (screen-slot-indexed) to `Config.priorities[]` (priority-rank-indexed)) gate loading.
  - `load()` — unrecognized magic → `set_defaults()` (and persists them); older version → `migrate()` (and persists the result); current version → full load.
  - `save()` — writes the entire `Config` back.
  - `save_priority(rank)` / `save_signal(signal_id)` — partial writes, so e.g. reassigning one rank or editing one signal's thresholds doesn't rewrite the whole blob.
  - `set_defaults()` — day/night brightness 100%/20%, 5 active panels (rank 0 = Coolant Temp down through rank 4 = Intake Pressure Actual, with rank 5 = Engine Torque inactive by default), all warning thresholds off, green accent.
  - `migrate(old_version)` — since no device has shipped with saved config yet, every version bump so far just falls through to `set_defaults()` rather than preserving old fields. **Once a version actually ships with saved user config in the field, this needs real field-by-field migration keyed on `old_version` instead of a blanket reset.**

## Usage

`src/main.cpp` owns the one `ConfigStore` instance (inside its `Composition` singleton), calls `load()` once at startup, and passes it into `gui::VehicleBinding` (see [gui/bindings](../../gui/bindings/README.md)), which is the only code that reads/writes `Config` fields on behalf of the UI.
