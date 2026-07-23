# TODO / Roadmap

Reworked from the old README's list against what's actually implemented as of the current codebase. Checked items are done and in the current build; unchecked items are not.

## Hardware

- [ ] Populate BOOT and RESET buttons on the PCB (provisions exist on the board, buttons just need to be ordered/installed).
- [ ] Physical redesign — smaller and more heat-resistant board/enclosure for permanent in-car mounting.
- [X] Touch screen support — SPI SDI/SCL lines were swapped on PCB v2; fixed on the schematic/PCB and confirmed working in the car.

## Software

- [X] EEPROM-backed configuration storage (panel assignments, warning thresholds, brightness, theme) — [core/config](core/config/README.md).
- [X] Warning light configuration with independent per-signal low/high thresholds — [core/vehicle/inc/warning.hpp](core/vehicle/inc/warning.hpp), [gui/screens/inc/warning_config_screen.hpp](gui/screens/inc/warning_config_screen.hpp).
- [X] Digital/Bar/Gauge display options, chosen per panel — [gui/widgets](gui/widgets/README.md).
- [X] 1–6 panel layouts, each with its own shape (horizontal orientation) — [gui/screens/inc/layout.hpp](gui/screens/inc/layout.hpp).
- [X] Overall appearance customization — accent color picker + swappable `Theme` — [gui/theme](gui/theme/README.md).
- [X] Manual day/night brightness toggle (two independently-set levels, switches instantly).
- [ ] **Automatic** day/night brightness switching (by clock or a light sensor) — currently only a manual toggle; explicitly deferred pending sensor hardware or a scheduling design.
- [ ] Vertical layout orientation has no UI to select it yet — `core::LayoutOrientation::Vertical` and `gui::compute_layout()`'s transpose path both exist and are exercised by tests, but nothing in Settings lets a user actually switch to it.
- [ ] Additional signal sources: transmission data (temp, clamping pressure, etc.), Haldex data, and similar beyond the current 22-signal engine/OBD2 catalog.
- [ ] Permanent power and sleep mode — currently powered from ignition (off whenever the car is off); running off permanent power would need a low-power sleep mode instead.
- [ ] Micro-SD card support.
- [ ] Data logging (record signal history to Micro-SD).
- [X] Wireless connectivity to a phone app for customization/setup — also the intended home for real per-vehicle range/threshold tuning (today's `SignalSpec::display_min/max` are sensible engineering defaults, not tuned per car). The SoftAP + QR-code hookup and the phone-facing config form ([gui/screens/inc/wireless_setup_screen.hpp](gui/screens/inc/wireless_setup_screen.hpp), [platform/esp32](platform/esp32/README.md)) both work end to end — the form covers everything the touchscreen does (brightness, panel/signal/units/display-mode assignment, warning thresholds) plus per-signal custom display-range, which has no touchscreen UI at all.
- [ ] ISO-TP support for CAN (multi-frame UDS responses) — current `core::can::UdsClient` only decodes single-frame responses.
- [ ] Firmware update strategy (OTA or otherwise).
- [ ] Expand to other vehicle platforms — the `platform/` HAL split and `core/vehicle` signal catalog make this architecturally possible, but the current catalog is still MQB-specific and no second platform is implemented.
- [X] ESP32-C3 relay firmware skeleton — [platform/esp32](platform/esp32/README.md) drives the SoftAP and speaks `remote::protocol`'s `CMD_AP_ENABLE`/`CMD_AP_DISABLE`/`EVT_AP_READY`/`EVT_AP_DISABLED`. It's a standalone sketch (`env:esp32c3`), not another `platform.hpp` `Platform` implementation — the C3 has no display/touch/CAN to back those interfaces.

## Manual validation still needed

- [X] On-hardware check of `gui/screens/inc/layout.hpp`'s `HERO_COLSPAN`/`STACK_COLSPAN` split (the 3-panel layout's hero-vs-stack width ratio) — currently a starting guess, not yet confirmed to read well at arm's length in the car.
- [ ] Real-vehicle validation of `SignalSpec::display_min/max` range defaults used to scale Bar/Gauge fills.
