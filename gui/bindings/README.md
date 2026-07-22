# gui/bindings/

`VehicleBinding` — the sole bridge between `core::` vehicle data and the `gui::` presentation layer — plus `RemoteBinding`, the analogous bridge between `remote::server`'s AP lifecycle and `gui::WirelessSetupScreen`. This is the only code in `gui/` allowed to include a `core/*` or `remote/*` header alongside a `gui/widgets`/`gui/screens` header.

## Files

- **`inc/vehicle_binding.hpp`** / **`src/vehicle_binding.cpp`** — `VehicleBinding(core::SignalStore &, core::ConfigStore &)`:
  - `build_view_model(rank, large, hero)` — reads whichever signal `rank` (priority rank, 0 = most important — not a screen slot) is currently assigned to (from `core::Config::priorities[]`), pulls its live value from `core::SignalStore`, and returns a `gui::PanelViewModel` (see [gui/widgets](../widgets/README.md)). `large`/`hero` are layout decisions passed through, not derived from vehicle data — see [gui/screens](../screens/README.md).
  - `apply_configurator_change(rank, signal_id, units, mode)` — reassigns a rank to a different signal/unit system/display mode, persisted immediately via `ConfigStore::save_priority()`.
  - `read_warning_thresholds(signal_id, is_metric)` / `apply_warning_change(signal_id, vm)` — read/write a signal's `WarningThresholds` (see [core/vehicle](../../core/vehicle/README.md)), converting between the signal's native (metric) storage unit and whichever unit system the panel that opened the editor is currently displaying in. `core::UnitType` conversion happens here — [gui/screens](../screens/README.md)'s `WarningConfigScreen` never needs to know about it.

  Also translates between `core::` and `gui::` versions of the same concept that are kept deliberately separate (e.g. `core::DisplayMode` ↔ `gui::DisplayMode`, `core::UnitSystem` ↔ a plain `is_metric` bool) — the `gui::` side never depends on `core::` types directly, even here; the translation happens inline.

- **`inc/remote_binding.hpp`** / **`src/remote_binding.cpp`** — `RemoteBinding(remote::server::RemoteApController &)`:
  - `activate()` / `deactivate()` — pass straight through to `RemoteApController`.
  - `build_view_model()` — maps `ApState` (`Idle`/`Activating`/`Active`/`Deactivating`) to a `gui::WirelessSetupViewModel` (`gui/screens/wireless_setup_screen.hpp`) — `Deactivating` folds into `Activating`'s "please wait" phase rather than getting its own, since it's brief and only ever entered by the user's own close (X) tap. While `Active`, also builds the `qr_payload`/`url_text` (`"http://<ip>"`, the config page — the AP is deliberately open, no password, so there's no Wi-Fi-join string to encode) from `RemoteApController::ssid()`/`ip_text()`.

## Usage

`src/main.cpp` owns the one `VehicleBinding` and one `RemoteBinding` instance. `VehicleBinding` wires into `InfoScreen`'s view-model builder callback and into the Configurator/Warning-Config screens' `DoneCallback`s — if you're adding a new piece of vehicle data a screen needs to show or edit, this is almost always the file to extend. `RemoteBinding` wires into `SettingsScreen`'s Wireless Setup row and `WirelessSetupScreen`'s Activate/Turn-Off callbacks, plus a periodic `core::Scheduler` task that refreshes the view-model while the screen is open (see `WIRELESS_SCREEN_REFRESH_MS` in `main.cpp`).
