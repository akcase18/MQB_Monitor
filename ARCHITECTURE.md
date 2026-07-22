# Architecture

MQB Monitor is split into four layers with a strict, one-way dependency rule:

```
platform/  →  core/         →  gui/bindings  →  gui/screens, gui/widgets
           →  remote/server ↗                ↑
                                           gui/theme
```

- **`platform/`** knows nothing above it. It's the only place that touches Arduino, FlexCAN_T4, SDL2, EEPROM.h, or a raw serial port directly.
- **`core/`** only includes `platform/platform.hpp` (the interfaces, never a concrete `teensy_*`/`simulator_*` class) plus the standard library. It has zero LVGL, zero Arduino, zero SDL — it builds and unit-tests on a plain host compiler.
- **`remote/protocol`** has zero deps (compiles into both the Teensy and ESP32-C3 firmwares); **`remote/server`** builds on it plus `platform/platform.hpp` (`ISerialLink`, `IClock`) and `core/config` — same platform-agnostic, host-testable discipline as `core/`.
- **`gui/widgets`** and **`gui/screens`** only include `gui/theme` and their own plain-data view-model structs. They never include anything from `core/`, `remote/`, or `platform/` — every screen and widget can be driven with fabricated data, independent of real CAN/config/AP wiring.
- **`gui/bindings`** is the *only* place allowed to include both a `core/*`/`remote/*` header and a `gui/widgets`/`gui/screens` header. It's the sole translator between vehicle/AP data and the UI — `VehicleBinding` for the former, `RemoteBinding` for the latter.
- **`src/main.cpp`** is the composition root: the only file that calls a concrete `platform_get()`, and the only place outside `gui/bindings` that depends on both `core::`/`remote::` and `gui::` directly.

This means a given `.cpp` file's `#include`s tell you which layer it belongs to, and there are no `#ifdef PLATFORM_TEENSY`/`#ifdef PLATFORM_SIMULATOR` branches anywhere outside `platform/` itself — `platformio.ini` picks which board's `platform/<board>/src` files get compiled into a given build (see [Build system](#build-system) below), so the rest of the codebase never needs to know which board it's running on.

## Folders

| Folder | What it is | README |
|---|---|---|
| `platform/` | Hardware abstraction interfaces (`ICanBus`, `IDisplay`, `ITouch`, `IConfigStore`, `IClock`) and one implementation per board. | [platform/README.md](platform/README.md) |
| `platform/teensy/` | Real hardware: FlexCAN_T4, ILI948x_t4_mm, SPI touch, EEPROM. | [platform/teensy/README.md](platform/teensy/README.md) |
| `platform/simulator/` | Desktop stand-in: raw SDL2 window/mouse, a file-backed config store, and a synthetic CAN bus that echoes fabricated responses. | [platform/simulator/README.md](platform/simulator/README.md) |
| `core/` | Platform-agnostic vehicle domain logic — scheduling, UDS protocol, the signal catalog/store, and the config schema. Zero LVGL/Arduino/SDL. | [core/README.md](core/README.md) |
| `core/time/` | `Scheduler` — fixed-capacity periodic task runner, the CAN polling heartbeat. | [core/time/README.md](core/time/README.md) |
| `core/can/` | `UdsClient` — UDS request/response protocol over `ICanBus`. | [core/can/README.md](core/can/README.md) |
| `core/vehicle/` | The 22-signal catalog, the runtime value store, and warning-threshold evaluation. | [core/vehicle/README.md](core/vehicle/README.md) |
| `core/config/` | Persisted config schema (`Config`/`Settings`/`PrioritizedSignal`/`SignalConfig`) and `ConfigStore`'s load/save/migrate logic over `IConfigStore`. | [core/config/README.md](core/config/README.md) |
| `remote/protocol/` | Wire format for the Teensy↔ESP32-C3 link: message types, framing, CRC, per-record encode/decode. Zero deps — compiles into both firmwares. | — |
| `remote/server/` | Teensy-side: `RemoteConfigServer` (batch-validates and applies a config over the wire), `RemoteConfigLink` (UART bytes ↔ frames), `RemoteApController` (AP lifecycle state machine). | — |
| `gui/` | The LVGL presentation layer. | [gui/README.md](gui/README.md) |
| `gui/theme/` | Palette/typography/metrics tokens — the one file a reskin touches. | [gui/theme/README.md](gui/theme/README.md) |
| `gui/widgets/` | `PanelWidget` — the single reusable data-panel building block. | [gui/widgets/README.md](gui/widgets/README.md) |
| `gui/screens/` | Every screen (Info, Configurator, Signal Picker, Warning Config, Settings, Theme Picker, Wireless Setup) plus the panel-layout algorithm and shared screen helpers. | [gui/screens/README.md](gui/screens/README.md) |
| `gui/bindings/` | `VehicleBinding` — the sole bridge between `core::` data and `gui::` view-models — and `RemoteBinding`, the same for `remote::server`'s AP lifecycle. | [gui/bindings/README.md](gui/bindings/README.md) |
| `gui/assets/` | Font source files and their licenses. | [gui/assets/README.md](gui/assets/README.md) |
| `src/` | `main.cpp`, the composition root. | (see below) |
| `support/` | PlatformIO pre/post build scripts for the simulator (SDL2 discovery, the "execute" target). | [support/README.md](support/README.md) |
| `test/` | Host-buildable Unity test suites. | [test/README.md](test/README.md) |
| `lib/` | Vendored third-party libraries (LVGL, FlexCAN_T4, ILI948x_t4_mm) — each has its own upstream README/license, not documented here. | — |

## `src/main.cpp` — the composition root

`main.cpp` is deliberately the one file that's allowed to know about everything: it calls `platform_get()` to obtain the board's `Platform`, constructs the `core::` stack (`ConfigStore`, `SignalStore`, `UdsClient`, `Scheduler`) on top of it, builds `gui::VehicleBinding` on top of that, and then builds and wires every `gui::` screen. Its `setup()`/`loop()` functions are the actual Arduino-style entry points on both boards (the simulator env supplies its own `main()` that calls them in a loop — see the bottom of the file).

Two points worth knowing if you're changing wiring here:
- **`runtime_theme()`** is a mutable *copy* of `gui::THEME_DEFAULT` that every screen is actually built against — not the `const` global directly — so a user-picked accent color has somewhere to write. Changing the accent rebuilds every screen from scratch against the updated copy (`rebuild_screens_for_theme_change()`), since LVGL styles don't re-read a `Theme` struct live.
- **`Composition`** is a Meyer's-singleton struct (function-local `static`) holding every long-lived object, constructed lazily on first access so it can safely depend on `platform_get()` without static-initialization-order concerns.

## Build system

`platformio.ini` defines two environments, `teensymm` and `simulator`. Both compile the same `core/` and `gui/` sources; the only thing that differs is which `platform/<board>/src` directory is included (via `build_src_filter`'s `+<...>`/`-<...>` entries) and a `-D PLATFORM_TEENSY` / `-D PLATFORM_SIMULATOR` define (used only inside that board's own `platform/<board>` files, never in `core/` or `gui/`). There's no third `esp32` environment yet — see [TODO.md](TODO.md).

The simulator environment also runs two build-time scripts from `support/` (SDL2 include/lib discovery, and wiring PlatformIO's `upload`/"execute" target to just run the compiled binary) — see [support/README.md](support/README.md).

## Where do I make change X?

| I want to... | Look at... |
|---|---|
| Add or change a monitored vehicle signal (UDS request, decode transform, units) | `core/vehicle/inc/signal_catalog.hpp` + `.cpp` — [core/vehicle](core/vehicle/README.md) |
| Change a signal's default warning thresholds or display range | `core/vehicle/src/signal_catalog.cpp` (`display_min`/`display_max`), `core/config/src/config_store.cpp` (`set_defaults()`) |
| Change which signals the 6 panel slots default to | `core::ConfigStore::set_defaults()` — [core/config](core/config/README.md) |
| Change colors, fonts, or spacing (reskin) | `gui/theme/src/theme.cpp` — [gui/theme](gui/theme/README.md) |
| Add a new accent color preset | `gui::ACCENT_PRESETS` in `gui/theme/src/theme.cpp` |
| Change how a panel is drawn (Digital/Bar/Gauge visuals) | `gui/widgets/src/panel_widget.cpp` — [gui/widgets](gui/widgets/README.md) |
| Change the shape of the 1–6 panel layouts | `gui/screens/inc/layout.hpp` + `src/layout.cpp` (`compute_layout()`) — [gui/screens](gui/screens/README.md) |
| Add a new screen | `gui/screens/` (new file, follow an existing screen's pattern) + wire it into `src/main.cpp`'s `build_screens()`/`destroy_screens()` |
| Change what happens when a panel is tapped, or how the Configurator/Warning/Settings screens hand data back to persistence | `gui/bindings/src/vehicle_binding.cpp` — [gui/bindings](gui/bindings/README.md), and the callback wiring in `src/main.cpp` |
| Change the persisted config schema | `core/config/inc/config.hpp` (bump `ConfigStore::CURRENT_VERSION` in `config_store.hpp` when you do) — [core/config](core/config/README.md) |
| Change CAN polling cadence or add a new periodic task | `core::Scheduler` calls in `src/main.cpp`'s `setup()` — [core/time](core/time/README.md) |
| Add support for a new board | New `platform/<board>/` implementing every interface in `platform/platform.hpp`, plus a new `[env:<board>]` in `platformio.ini` — [platform](platform/README.md) |
| Change what the desktop simulator's fabricated CAN data looks like | `platform/simulator/src/simulator_can.cpp` — [platform/simulator](platform/simulator/README.md) |

## Testing

`test/` holds host-buildable Unity suites covering the logic that's actually had bugs before (unit conversion/rounding, config migration, layout math) — see [test/README.md](test/README.md). Run them with `pio test -e simulator`; note that this overwrites the simulator's build output, so run `pio run -e simulator` afterward to restore the interactive binary before using it again.
