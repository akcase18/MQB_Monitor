# gui/bindings/

`VehicleBinding` — the sole bridge between `core::` vehicle data and the `gui::` presentation layer — plus `RemoteBinding`, the analogous bridge between `remote::server`'s AP lifecycle and `gui::WirelessSetupScreen`. This is the only code in `gui/` allowed to include a `core/*` or `remote/*` header alongside a `gui/widgets`/`gui/screens` header.

## Files

- **`inc/vehicle_binding.hpp`** / **`src/vehicle_binding.cpp`** — `VehicleBinding(core::SignalStore &, core::ConfigStore &)`:
  - `build_view_model(rank, large, hero)` — reads whichever signal `rank` (priority rank, 0 = most important — not a screen slot) is currently assigned to (from `core::Config::priorities[]`), pulls its live value from `core::SignalStore`, and returns a `gui::PanelViewModel` (see [gui/widgets](../widgets/README.md)). `large`/`hero` are layout decisions passed through, not derived from vehicle data — see [gui/screens](../screens/README.md).
  - `apply_configurator_change(rank, signal_id, units, mode)` — reassigns a rank to a different signal/unit system/display mode, persisted immediately via `ConfigStore::save_priority()`.
  - `set_warning_latch(latch)` — sources the panels' warning highlight from a `core::WarningLatch` (see [core/vehicle](../../core/vehicle/README.md)) instead of evaluating the thresholds live. Null is the default, so the CAN product's behaviour is unchanged; the gauge product sets it, which is what makes a 200 ms pressure dip stay lit after the value has come back inside.

    **Read-only, and that is the whole design.** The latch is *driven* by whoever owns it, on its own cadence, over every signal in the catalog. Driving it from `build_view_model()` would tie qualification to whatever is currently being drawn — and the gauge product's zoom view draws one panel of three, so the two signals off screen would stop qualifying and a warning would depend on where the driver happened to be looking. `test_gui_bindings` asserts that drawing a panel never advances the latch, with `qualify_ms` set to 0 so the assertion is not vacuous.
  - `read_warning_thresholds(signal_id, is_metric)` / `apply_warning_change(signal_id, vm)` — read/write a signal's `WarningThresholds` (see [core/vehicle](../../core/vehicle/README.md)), converting between the signal's native (metric) storage unit and whichever unit system the panel that opened the editor is currently displaying in. `core::UnitType` conversion happens here — [gui/screens](../screens/README.md)'s `WarningConfigScreen` never needs to know about it.

  Also translates between `core::` and `gui::` versions of the same concept that are kept deliberately separate (e.g. `core::DisplayMode` ↔ `gui::DisplayMode`, `core::UnitSystem` ↔ a plain `is_metric` bool) — the `gui::` side never depends on `core::` types directly, even here; the translation happens inline.

- **`inc/remote_binding.hpp`** / **`src/remote_binding.cpp`** — `RemoteBinding(remote::server::RemoteApController &)`:
  - `activate()` / `deactivate()` — pass straight through to `RemoteApController`.
  - `build_view_model()` — maps `ApState` (`Idle`/`Activating`/`Active`/`Deactivating`) to a `gui::WirelessSetupViewModel` (`gui/screens/wireless_setup_screen.hpp`) — `Deactivating` folds into `Activating`'s "please wait" phase rather than getting its own, since it's brief and only ever entered by the user's own close (X) tap. While `Active`, also builds the `qr_payload`/`url_text` (`"http://<ip>"`, the config page — the AP is deliberately open, no password, so there's no Wi-Fi-join string to encode) from `RemoteApController::ssid()`/`ip_text()`.

## Usage

Each composition root owns one `VehicleBinding`; `RemoteBinding` is the CAN product's only, since the gauge board has no companion ESP32-C3 to talk to.

`src/main.cpp` owns the one `VehicleBinding` and one `RemoteBinding` instance. `VehicleBinding` wires into `InfoScreen`'s view-model builder callback and into the Configurator/Warning-Config screens' `DoneCallback`s — if you're adding a new piece of vehicle data a screen needs to show or edit, this is almost always the file to extend. `RemoteBinding` wires into `SettingsScreen`'s WiFi row and `WirelessSetupScreen`'s Activate/Turn-Off callbacks, plus a periodic `core::Scheduler` task that refreshes the view-model while the screen is open (see `WIRELESS_SCREEN_REFRESH_MS` in `main.cpp`).
