# gui/bindings/

`VehicleBinding` — the sole bridge between `core::` vehicle data and the `gui::` presentation layer. This is the only code in `gui/` allowed to include a `core/*` header alongside a `gui/widgets`/`gui/screens` header.

## Files

- **`inc/vehicle_binding.hpp`** / **`src/vehicle_binding.cpp`** — `VehicleBinding(core::SignalStore &, core::ConfigStore &)`:
  - `build_view_model(panel_idx, large, hero)` — reads whichever signal `panel_idx` is currently assigned to (from `core::Config::panels[]`), pulls its live value from `core::SignalStore`, and returns a `gui::PanelViewModel` (see [gui/widgets](../widgets/README.md)). `large`/`hero` are layout decisions passed through, not derived from vehicle data — see [gui/screens](../screens/README.md).
  - `apply_configurator_change(panel_idx, signal_id, units, mode)` — reassigns a panel to a different signal/unit system/display mode, persisted immediately via `ConfigStore::save_panel()`.
  - `read_warning_thresholds(signal_id, is_metric)` / `apply_warning_change(signal_id, vm)` — read/write a signal's `WarningThresholds` (see [core/vehicle](../../core/vehicle/README.md)), converting between the signal's native (metric) storage unit and whichever unit system the panel that opened the editor is currently displaying in. `core::UnitType` conversion happens here — [gui/screens](../screens/README.md)'s `WarningConfigScreen` never needs to know about it.

  Also translates between `core::` and `gui::` versions of the same concept that are kept deliberately separate (e.g. `core::DisplayMode` ↔ `gui::DisplayMode`, `core::UnitSystem` ↔ a plain `is_metric` bool) — the `gui::` side never depends on `core::` types directly, even here; the translation happens inline.

## Usage

`src/main.cpp` owns the one `VehicleBinding` instance and wires it into `InfoScreen`'s view-model builder callback and into the Configurator/Warning-Config screens' `DoneCallback`s. If you're adding a new piece of vehicle data a screen needs to show or edit, this is almost always the file to extend.
