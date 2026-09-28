# TODO / Roadmap

Reworked from the old README's list against what's actually implemented as of the current codebase. Checked items are done and in the current build; unchecked items are not.

## Hardware

- [X] Enclosure redesign - New heat-resistant enclosure and mounting for new PCB.
  - In Progress
- [X] PCB redesign - smaller board that utilizes the non-breakout LCD panel.
- [X] Populate BOOT and RESET buttons on the PCB (provisions exist on the board, buttons just need to be ordered/installed).
- [X] Touch screen support — SPI SDI/SCL lines were swapped on PCB v2; fixed on the schematic/PCB and confirmed working in the car.
- [X] Capacitive touch controller (FT6236, I2C) — replaces the resistive XPT2046 on the new PCB (panel now mounts directly to the board instead of via the old breakout board). Both drivers kept and switchable in code — [platform/teensy](platform/teensy/README.md).

## Software

- [X] EEPROM-backed configuration storage (panel assignments, warning thresholds, brightness, theme) — [core/config](core/config/README.md).
- [X] Warning light configuration with independent per-signal low/high thresholds — [core/vehicle/inc/warning.hpp](core/vehicle/inc/warning.hpp), [gui/screens/inc/warning_config_screen.hpp](gui/screens/inc/warning_config_screen.hpp).
- [X] Digital/Bar/Gauge display options, chosen per panel — [gui/widgets](gui/widgets/README.md).
- [X] 1–6 panel layouts, each with its own shape (horizontal orientation) — [gui/screens/inc/layout.hpp](gui/screens/inc/layout.hpp).
- [X] Overall appearance customization — accent color picker + swappable `Theme` — [gui/theme](gui/theme/README.md).
- [X] Manual day/night brightness toggle (two independently-set levels, switches instantly).
- [ ] **Automatic** day/night brightness switching (by clock or a light sensor) — currently only a manual toggle; explicitly deferred pending sensor hardware or a scheduling design.
- [X] Vertical (portrait) layout orientation — selectable from Settings (tap the Orientation toggle, confirm the popup). Physically rotates the display/touch panel and every screen's layout, not just a transposed `InfoScreen` panel grid inside a canvas that stayed landscape. Boot-time-only: the new orientation is persisted immediately but only takes visual effect after a restart, which the confirm popup states explicitly — see `platform.hpp`'s `DisplayOrientation`/`IDisplay::set_orientation()`.
- [ ] Additional signal sources: transmission data (temp, clamping pressure, etc.), Haldex data, and similar beyond the current 22-signal engine/OBD2 catalog.
- [X] Storage abstraction (platform-layer HAL only — mount + file I/O, not the logging format/rotation) — `platform::IStorage`. Internal MCU program flash (`LittleFS_Program`) is the active backend; Micro-SD is kept as a switchable alternate implementation for whenever a card slot is back in the design — [platform/teensy](platform/teensy/README.md).
- [X] Data logging (record signal history via `platform::IStorage`) — [core/logging](core/logging/README.md)'s `Logger` records the currently-displayed signals (up to 6) on-change to a rotating ring of chunk files (oldest overwritten once full, ≥~3 hours retention worst-case), retrievable over USB via [tools/log_export](tools/log_export/README.md). Opt-in via `LoggingScreen` (swipe up from the dashboard; `Settings::logging_enabled`, off by default) so normal driving isn't spending flash write-cycles. That screen also reports captures saved, storage medium, capacity, and minutes recorded, and can erase the store on-device behind a confirm popup. No on-device viewer GUI (not requested — capture + external retrieval only); a small InfoScreen status glyph shows whether logging is active.
- [ ] Wireless (phone) and Micro-SD export of the signal log — sequenced follow-ons to the USB-only retrieval above, both reusing the same on-disk `LogRecord`/`ChunkHeader` format over a different transport. Wireless needs new `remote::protocol` message types plus a chunking scheme (today's link is single-frame request/response only); Micro-SD needs a physical slot the current PCB doesn't have (the `TeensySdCard`/`IStorage` code path is already in place, see `platform/teensy/README.md`).
- [X] Wireless connectivity to a phone app for customization/setup — also the intended home for real per-vehicle range/threshold tuning (today's `SignalSpec::display_min/max` are sensible engineering defaults, not tuned per car). The SoftAP + QR-code hookup and the phone-facing config form ([gui/screens/inc/wireless_setup_screen.hpp](gui/screens/inc/wireless_setup_screen.hpp), [platform/esp32](platform/esp32/README.md)) both work end to end — the form covers everything the touchscreen does (brightness, panel/signal/units/display-mode assignment, warning thresholds) plus per-signal custom display-range, which has no touchscreen UI at all.
- [ ] ISO-TP support for CAN (multi-frame UDS responses) — current `core::can::UdsClient` only decodes single-frame responses.
- [ ] Firmware update strategy (OTA or otherwise), waiting for ESP32-S3.
- [ ] Expand to other vehicle platforms — the `platform/` HAL split and `core/vehicle` signal catalog make this architecturally possible, but the current catalog is still MQB-specific and no second platform is implemented.
- [X] ESP32-C3 relay firmware skeleton — [platform/esp32](platform/esp32/README.md) drives the SoftAP and speaks `remote::protocol`'s `CMD_AP_ENABLE`/`CMD_AP_DISABLE`/`EVT_AP_READY`/`EVT_AP_DISABLED`. It's a standalone sketch (`env:esp32c3`), not another `platform.hpp` `Platform` implementation — the C3 has no display/touch/CAN to back those interfaces.

## Manual validation still needed

- [X] On-hardware check of `gui/screens/inc/layout.hpp`'s `HERO_COLSPAN`/`STACK_COLSPAN` split (the 3-panel layout's hero-vs-stack width ratio) — currently a starting guess, not yet confirmed to read well at arm's length in the car.
- [X] Real-vehicle validation of `SignalSpec::display_min/max` range defaults used to scale Bar/Gauge fills.
- [X] FT6236 reset timing and touch coordinate orientation, both Landscape and Portrait — `TeensyCapTouch` (`platform/teensy/src/teensy_cap_touch.cpp`) is currently dormant (`TEENSY_TOUCH_USE_RESISTIVE=1`), so neither its Landscape zero-transform nor its Portrait transform (inferred by analogy with the resistive driver's confirmed fix) has actually been tested on this driver. The resistive driver's own Landscape/Portrait mappings are confirmed good on real hardware; that confirmation doesn't carry over to the capacitive driver, which needs its own once/if the board switches to it.
