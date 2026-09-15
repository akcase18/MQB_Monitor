# Architecture

MQB Monitor is split into four layers with a strict, one-way dependency rule:

```
gui/bindings  →  core/, remote/server, gui/screens, gui/widgets
core/, remote/server  →  platform/           (remote/server also → core/config)
gui/screens, gui/widgets  →  gui/theme
```

(read `A → B` as "A depends on / includes B")

- **`platform/`** knows nothing above it. It's the only place that touches Arduino, FlexCAN_T4, SDL2, EEPROM.h, or a raw serial port directly.
- **`core/`** only includes `platform/platform.hpp` (the interfaces, never a concrete `teensy_*`/`simulator_*` class) plus the standard library. It has zero LVGL, zero Arduino, zero SDL — it builds and unit-tests on a plain host compiler.
- **`core/vehicle` does not depend on `core/can`.** Inside `core/`, one more line is drawn: `core/vehicle` owns what a signal *is* (name, unit, display range, current value, warning state), and `core/can` owns how one is *obtained* (the UDS protocol, the request table, the decoder, the scheduler). Nothing in `core/vehicle` includes anything from `core/can` — `signal_catalog.hpp` and `signal_store.hpp` compile with `core/can/inc` absent from the include path entirely, and so, transitively, does `core/config`'s persisted schema. That is what lets a product acquiring the same signals from analog sensors supply its own acquisition layer without touching the catalog, the store, the warning evaluator, the config schema, or any of `gui/` — which is now not a claim but a fact: `core/sensors` is that second layer, and it added nothing to `core/vehicle`. The two halves are kept in step by `tools/gen_catalog.py`, which emits `core/vehicle`'s catalog and `core/can`'s binding table as parallel arrays from one pass over `signals.json`.
- **`remote/protocol`** has zero deps (compiles into both the Teensy and ESP32-C3 firmwares); **`remote/server`** builds on it plus `platform/platform.hpp` (`ISerialLink`, `IClock`) and `core/config` — same platform-agnostic, host-testable discipline as `core/`.
- **`gui/widgets`** and **`gui/screens`** only include `gui/theme` and their own plain-data view-model structs. They never include anything from `core/`, `remote/`, or `platform/` — every screen and widget can be driven with fabricated data, independent of real CAN/config/AP wiring.
- **`gui/bindings`** is the *only* place allowed to include both a `core/*`/`remote/*` header and a `gui/widgets`/`gui/screens` header. It's the sole translator between vehicle/AP data and the UI — `VehicleBinding` for the former, `RemoteBinding` for the latter.
- **A composition root** is the only file that calls a concrete `platform_get()`, and the only place outside `gui/bindings` that depends on both `core::`/`remote::` and `gui::` directly. There are two of them — `src/main.cpp` for the CAN dashboard and `src/main_gauge.cpp` for the analog gauge — and `platformio.ini` picks one per environment, the same mechanism that picks a `platform/<board>/src`. Neither carries a preprocessor branch.

This means a given `.cpp` file's `#include`s tell you which layer it belongs to, and there are no `#ifdef PLATFORM_TEENSY`/`#ifdef PLATFORM_SIMULATOR` branches anywhere outside `platform/` itself — `platformio.ini` picks which board's `platform/<board>/src` files get compiled into a given build (see [Build system](#build-system) below), so the rest of the codebase never needs to know which board it's running on.

## Folders

| Folder | What it is | README |
|---|---|---|
| `platform/` | Hardware abstraction interfaces (`ICanBus`, `IDisplay`, `ITouch`, `IConfigStore`, `ISerialLink`, `IStorage`, `IClock`) and one implementation per board. | [platform/README.md](platform/README.md) |
| `platform/teensy/` | Real hardware: FlexCAN_T4, ILI948x_t4_mm, EEPROM, a spare hardware `Serial`, and (switchable) I2C capacitive/SPI resistive touch and onboard-flash/Micro-SD storage. | [platform/teensy/README.md](platform/teensy/README.md) |
| `platform/simulator/` | Desktop stand-in: raw SDL2 window/mouse, a file-backed config store, and a synthetic CAN bus that echoes fabricated responses. | [platform/simulator/README.md](platform/simulator/README.md) |
| `platform/esp32s3/` | The analog gauge board: ST7789V2 over SPI, ADS1115 over I²C, two GPIO buttons, NVS config, LittleFS. **Never run on hardware.** | [platform/esp32s3/README.md](platform/esp32s3/README.md) |
| `core/` | Platform-agnostic vehicle domain logic — scheduling, UDS protocol, the signal catalog/store, and the config schema. Zero LVGL/Arduino/SDL. | [core/README.md](core/README.md) |
| `core/time/` | `Scheduler` — fixed-capacity periodic task runner, the CAN polling heartbeat. | [core/time/README.md](core/time/README.md) |
| `core/can/` | Everything that knows values arrive over CAN, behind one `core::ISignalSource`: `UdsSignalSource` (the facade), `UdsClient` (protocol), `UDS_BINDINGS` (which requests feed which signal), `UdsDecoder` (responses → stored values), `SignalPoller` (what to ask for, when). | [core/can/README.md](core/can/README.md) |
| `core/input/` | Physical buttons: debounce and gesture recognition per button, the chord recogniser between them, and the remappable `(source, gesture) → action` map. Deliberately not an `lv_indev` — buttons drive app logic directly and never navigate. Not built into `env:teensymm`. | [core/input/README.md](core/input/README.md) |
| `core/sensors/` | Everything that knows values arrive as volts, behind the same `core::ISignalSource`: `AnalogSignalSource`, the transfer functions and thermistor curve models, filtering, and the plausibility check that keeps a broken wire off the gauge. Not built into `env:teensymm`. | [core/sensors/README.md](core/sensors/README.md) |
| `core/vehicle/` | The 22-signal catalog (name/unit/display range), the runtime value store, and warning-threshold evaluation. Knows nothing about how a value is acquired. | [core/vehicle/README.md](core/vehicle/README.md) |
| `core/config/` | Persisted config schema (`Config`/`Settings`/`PrioritizedSignal`/`SignalConfig`) and `ConfigStore`'s load/save/migrate logic over `IConfigStore`. What the dashboard *shows*. | [core/config/README.md](core/config/README.md) |
| `core/device_config/` | The other half of persistence, for a product with sensors and buttons: `SensorCal[]`, the action map, and the warning qualify time. What the hardware *is*. Its own blob at its own offset — folding it into `Config` would make calibration ten times the size of everything else there, for hardware the CAN product does not have. Not built into `env:teensymm`. | [core/device_config/README.md](core/device_config/README.md) |
| `remote/protocol/` | Wire format for the Teensy↔ESP32-C3 link: message types, framing, CRC, per-record encode/decode. Zero deps — compiles into both firmwares. | — |
| `remote/server/` | Teensy-side: `RemoteConfigServer` (batch-validates and applies a config over the wire), `RemoteConfigLink` (UART bytes ↔ frames), `RemoteApController` (AP lifecycle state machine). | — |
| `gui/` | The LVGL presentation layer. | [gui/README.md](gui/README.md) |
| `gui/theme/` | Palette/typography/metrics tokens — the one file a reskin touches. | [gui/theme/README.md](gui/theme/README.md) |
| `gui/widgets/` | `PanelWidget` — the single reusable data-panel building block. | [gui/widgets/README.md](gui/widgets/README.md) |
| `gui/screens/` | Every screen (Info, Configurator, Signal Picker, Warning Config, Settings, Theme Picker, Wireless Setup) plus the panel-layout algorithm and shared screen helpers. | [gui/screens/README.md](gui/screens/README.md) |
| `gui/bindings/` | `VehicleBinding` — the sole bridge between `core::` data and `gui::` view-models — and `RemoteBinding`, the same for `remote::server`'s AP lifecycle. | [gui/bindings/README.md](gui/bindings/README.md) |
| `gui/assets/` | Font source files and their licenses. | [gui/assets/README.md](gui/assets/README.md) |
| `src/` | `main.cpp` and `main_gauge.cpp`, the two composition roots — one per product. | (see below) |
| `src/app/` | The parts of a composition root that are the same on every product: LVGL bring-up, the backlight blanking state machine, the serial log console, the orientation enum translations, and the off-Arduino `main()`. Shared by both roots, so each is left holding only wiring. | (see below) |
| `support/` | PlatformIO pre/post build scripts for the simulator (SDL2 discovery, the "execute" target). | [support/README.md](support/README.md) |
| `test/` | Host-buildable Unity test suites. | [test/README.md](test/README.md) |
| `lib/` | Vendored third-party libraries (LVGL, FlexCAN_T4, ILI948x_t4_mm) — each has its own upstream README/license, not documented here. | — |

## `src/main.cpp` and `src/main_gauge.cpp` — the composition roots

A root is deliberately the one file that's allowed to know about everything: it calls `platform_get()` to obtain the board's `Platform`, constructs the `core::` stack (`ConfigStore`, `SignalStore`, a `core::ISignalSource`, `Scheduler`) on top of it, builds `gui::VehicleBinding` on top of that, and then builds and wires every `gui::` screen. Its `setup()`/`loop()` functions are the actual Arduino-style entry points on both boards (the simulator env supplies its own `main()` that calls them in a loop — see the bottom of the file).

**It holds wiring and nothing else.** Everything that was a *subsystem* rather than an assembly decision has been lifted into `src/app/`, which cut it from 1,380 lines to ~950 and — the part worth noticing — to **zero preprocessor branches**. What moved:

| `src/app/` | What it owns |
|---|---|
| `backlight.*` | The blank-on-boot / blank-on-rebuild state machine and its LVGL watchdog timer. Owns policy (when is a transition "over"?) and state across many call sites; the root now just reports that a flush landed or a rebuild is starting. |
| `lvgl_runtime.*` | `lv_init()`, the partial-render draw buffers, the display, the tick source, and optionally a pointer indev. One call. `with_pointer_indev` is the only real product difference — a touchless board must not create one. |
| `log_console.*` | The USB-serial `LOGDUMP`/`LOGCLEAR`/`LOGSTATUS` command set. Its `#ifdef ARDUINO` lives here, so `setup()`/`loop()` call it unconditionally. |
| `orientation.hpp` | The three-way translation between `core::LayoutOrientation`, `gui::LayoutOrientation` and `DisplayOrientation`. Inline; compiles to nothing. |
| `entry_point.*` | `setup()`/`loop()` declared with the linkage the Arduino cores expect (they are `extern "C"`, via `wiring.h`), plus the `main()` that stands in off-hardware. Without the declarations a root links fine on the host and fails only in the firmware build. |

The refactor was verified as a pure move — though see the note on measuring that under [Build system](#build-system): the Teensy image's *total* flash is a padded number and stays at 505,852 bytes through changes that do move real code.

### The second root

`src/main_gauge.cpp` is the analog gauge product, selected by `env:simulator_gauge`'s `build_src_filter`. It shares `core/time`, `core/vehicle`, `core/config`, `core/logging`, all of `gui/` and all of `src/app/` with `main.cpp`, and differs in exactly three places:

| | `src/main.cpp` | `src/main_gauge.cpp` |
|---|---|---|
| Acquisition | `core::UdsSignalSource` over `ICanBus` | `core::AnalogSignalSource` over `IAnalogInput` |
| Input | an `lv_indev` over `ITouch` | `core::ButtonInput` + `core::ActionMap` over `IButtons`, driving app logic directly — **no `lv_indev`, no `lv_group`** |
| Presentation | eight screens, `LayoutStyle::Grid`, `THEME_DEFAULT` | one screen, `LayoutStyle::Strip`, `THEME_GAUGE`, a `gui::ZoomCycle` instead of navigation, and a `gui::ToastOverlay` for everything a press needs to acknowledge |

"No `lv_indev`" is the largest simplification the touchless design buys, and it holds only as long as buttons never navigate. The moment a gesture moves focus rather than performing an action, this product needs all the LVGL input plumbing the other one has.

Three points worth knowing if you're changing wiring in either:
- **`runtime_theme()`** is a mutable *copy* of the product's `const gui::Theme` that every screen is actually built against — not the `const` global directly — so a user-picked accent color has somewhere to write. Changing the accent rebuilds every screen from scratch against the updated copy (`rebuild_screens_for_theme_change()`), since LVGL styles don't re-read a `Theme` struct live.
- **Acquisition reaches a root only through `core::ISignalSource`** — `set_active()` to say which signals are wanted, `tick()` once per `loop()` pass. The concrete source is named exactly twice, in its declaration and its constructor call. That is not a claim any more: the two roots differ there and nowhere else on the acquisition path, and nothing in `core/vehicle`, `core/config` or `gui/` knows whether a bus or an ADC is behind it.
- **`Composition`** is a Meyer's-singleton struct (function-local `static`) holding every long-lived object, constructed lazily on first access so it can safely depend on `platform_get()` without static-initialization-order concerns.

## Build system

`platformio.ini` selects everything product- and board-specific, which is what lets the source tree carry no `#ifdef PLATFORM_*` outside `platform/`. Three separate things are picked per environment, all by the same mechanism:

| Picked | How |
|---|---|
| The board | `build_src_filter` includes one `platform/<board>/src`, plus a `-D PLATFORM_TEENSY`/`-D PLATFORM_SIMULATOR` used only inside that board's own files |
| The product's composition root | `build_src_filter` includes `src/main.cpp` **or** `src/main_gauge.cpp`. `src/` is listed explicitly rather than globbed — a `+<*>` would compile both roots into one binary |
| The product's signal catalog | the include path carries one `core/vehicle/generated/<product>/` (and, for a product with a bus, one `core/can/generated/<product>/`) |

Environments: `teensymm` (the CAN dashboard firmware), `simulator` (that product on the desktop), `esp32s3_gauge` (the analog gauge firmware), `simulator_gauge` (that product on the desktop), `esp32c3` (the companion Wi-Fi relay for the CAN product), `web_preview` (host-native render of the phone config form), and `can_sniffer` (a standalone passive CAN→USB tool sharing nothing but a library).

`esp32s3_gauge` and `simulator_gauge` differ **only** in which `platform/<board>/src` they compile. Same composition root, same catalog, same everything above `platform/` — which is the clearest statement of what the layering is for. Note that `esp32s3_gauge` also needs `build_unflags = -std=gnu++11`: Arduino-ESP32 2.x compiles at C++11 by default, and `core/` has always been C++17.

The simulator environments also run two build-time scripts from `support/` (SDL2 include/lib discovery, and wiring PlatformIO's `upload`/"execute" target to just run the compiled binary) — see [support/README.md](support/README.md).

### Measuring whether a change cost anything on the Teensy

`teensy_size`'s FLASH line reports `code`, `data` and `headers`, and it is tempting to add them up and compare totals. **Do not** — `code + headers` is padded to a fixed size, so growth in one is absorbed by the other and the total sits at 505,852 bytes across changes that genuinely moved hundreds of bytes of code. RAM1's `code + padding` behaves the same way (it pads to a 32 KB ITCM boundary). The figures that actually move are `FLASH: data`, `RAM1: variables` and `RAM2: variables`; watch `code` on its own for instruction growth, and treat the total only as "did this cross a padding boundary".

## Where do I make change X?

| I want to... | Look at... |
|---|---|
| Add or change a monitored vehicle signal | That product's `core/vehicle/data/<product>/signals.json`, then re-run `tools/gen_catalog.py` — [core/vehicle](core/vehicle/README.md). **The two products share no signals at all** (an MQB car cannot report oil pressure), so each has its own registry and its own generated output; `platformio.ini` puts exactly one product's `generated/` directory on the include path per env |
| Change a signal's default warning thresholds or display range | `core/vehicle/src/signal_catalog.cpp` (`display_min`/`display_max`), `core/config/src/config_store.cpp` (`set_defaults()`) |
| Change which signals the 6 panel slots default to | `core::ConfigStore::set_defaults()` — [core/config](core/config/README.md) |
| Change colors, fonts, or spacing (reskin) | `gui/theme/src/theme.cpp` — [gui/theme](gui/theme/README.md) |
| Add a new accent color preset | `gui::ACCENT_PRESETS` in `gui/theme/src/theme.cpp` |
| Change how a panel is drawn (Digital/Bar/Gauge visuals) | `gui/widgets/src/panel_widget.cpp` — [gui/widgets](gui/widgets/README.md) |
| Change the shape of the 1–6 panel layouts | `gui/screens/inc/layout.hpp` + `src/layout.cpp` (`compute_layout()`) — [gui/screens](gui/screens/README.md). `LayoutStyle::Grid` is pinned by a golden table in `test_gui_layout`; changing it means editing that table on purpose |
| Add a theme for a different panel size | A new `const Theme` in `gui/theme/src/theme.cpp` (see `THEME_GAUGE`) — [gui/theme](gui/theme/README.md). Check the font fit arithmetically and add it to `test_gui_theme`; the readout that clips is always the one nobody thought to look at |
| Change boot/rebuild backlight blanking behaviour | `src/app/backlight.cpp` — the reasoning for why the two paths wait on *different* signals, and why the obvious unification does not work, is in that file next to the code it constrains |
| Change LVGL draw-buffer size for a smaller panel | `LVGL_DRAW_BUF_PIXELS`, a build flag read by `src/app/lvgl_runtime.cpp` (defaults to one tenth of 480×320). A tunable, not a board branch |
| Add a new screen | `gui/screens/` (new file, follow an existing screen's pattern) + wire it into `src/main.cpp`'s `build_screens()`/`destroy_screens()`, and add it to `env:simulator_gauge`'s `build_src_filter` only if the gauge product actually shows it — that env names the four screen sources it wants rather than the directory |
| Say something transient to the user without adding a screen | `gui::ToastOverlay` on `lv_layer_top()` — [gui/widgets](gui/widgets/README.md). The whole point of the touchless product is that there is nothing to navigate into and back out of |
| Change what happens when a panel is tapped, or how the Configurator/Warning/Settings screens hand data back to persistence | `gui/bindings/src/vehicle_binding.cpp` — [gui/bindings](gui/bindings/README.md), and the callback wiring in `src/main.cpp` |
| Change which signals a product shows out of the box | the `defaults` block in that product's `signals.json` (`num_panels`, `priority_ids`) — it generates `signal_defaults.generated.inc`, which `ConfigStore::set_defaults()` reads. It used to be hardcoded MQB ids, which were silently wrong for any other product |
| Change the persisted config schema | `core/config/inc/config.hpp` (bump `ConfigStore::CURRENT_VERSION` in `config_store.hpp` when you do) — [core/config](core/config/README.md) |
| Change how often a signal is polled | that signal's `cycle_time_ms` in `core/vehicle/data/mqb/signals.json` (re-run `tools/gen_catalog.py`; it lands in core/can's binding table). It is a floor, not a guarantee — `core::SignalPoller` polls no *faster* than this and slower when the ECU can't keep up — [core/can](core/can/README.md) |
| Change which signals get polled at all, or how requests are paced | `core::SignalPoller` — [core/can](core/can/README.md). Only displayed panels are polled; `src/main.cpp`'s `sync_active_signals()` is what tells it which those are, via `ISignalSource::set_active()` |
| Add a new periodic task | `core::Scheduler` calls in the relevant root's `setup()` — [core/time](core/time/README.md) |
| Change how a warning behaves over time (latching, how long a crossing must persist) | `core::WarningLatch` — [core/vehicle](core/vehicle/README.md). `evaluate_warning()` stays stateless and untouched; the latch wraps it. The root ticks it over every signal on a timer, and `gui::VehicleBinding` only *reads* it, because a view built from the panels on screen would stop qualifying the ones that are not |
| Change what a button gesture does, or add a new action | `core::ActionMap::set_defaults()` in `core/input/src/action_map.cpp` for the defaults, `core::InputAction` for a new action — [core/input](core/input/README.md). Bindings are three-byte records built to be edited from the web form, so *remapping* needs no firmware change at all |
| Change a gesture threshold (debounce, long-press, repeat rate) | `core::GestureTimings` in `core/input/inc/button_gesture.hpp` — [core/input](core/input/README.md). The tests compute expected instants from these, so moving one moves them with it |
| Change what a sensor on an ADC channel is, or add a preset for one | `core::SensorCal` in `core/sensors/inc/sensor_cal.hpp`, `sensor_preset_*()` in `curve_library.cpp` — [core/sensors](core/sensors/README.md). A preset only *fills in* a calibration; nothing reads a preset id at runtime, so changing the table cannot change what an already-configured gauge reads |
| Acquire signals some other way (a different bus, analog sensors) | New `core::ISignalSource` implementation (see `core/vehicle/inc/signal_source.hpp`) plus its own acquisition table; swap the two lines in the root that name the concrete source. Nothing in `core/vehicle`, `core/config` or `gui/` changes — `src/main_gauge.cpp` is the worked example |
| Add a whole new product (different panel, different sensors, one screen) | A new `src/main_<product>.cpp`, a new `core/vehicle/data/<product>/signals.json`, and a new `[env:]` naming both. No existing file needs an `#ifdef` |
| Add support for a new board | New `platform/<board>/` implementing every interface in `platform/platform.hpp` (reaching for `platform/null_*.hpp` for whatever it does not have), plus a new `[env:<board>]` — [platform](platform/README.md). `platform/esp32s3/` is the worked example and added no line anywhere above `platform/`. Two traps: `setup()`/`loop()` linkage differs per Arduino core (see `src/app/entry_point.hpp`), and the pinout belongs in one header as `-D`s, not inside drivers |
| Persist something new about the *hardware* — a sensor field, a gesture binding | `core::DeviceConfig` — [core/device_config](core/device_config/README.md). Bump `DEVICE_CONFIG_VERSION`; the header's `struct_size` fingerprint means a stale blob is rejected rather than read at the wrong size. Not `core::Config`, which is the shared display schema |
| Change what the desktop simulator's fabricated CAN data looks like | `platform/simulator/src/simulator_can.cpp` — [platform/simulator](platform/simulator/README.md) |
| Change what the desktop simulator's fabricated *sensor* data looks like, or add a fault you can trigger by key | `platform/simulator/src/simulator_analog_input.cpp` and `simulator_events.cpp` — [platform/simulator](platform/simulator/README.md). Keep anything key-driven out of `microvolts_at()`, which host tests rely on being a pure function of its arguments |

## Testing

`test/` holds host-buildable Unity suites covering the logic that's actually had bugs before (unit conversion/rounding, config migration, layout math) — see [test/README.md](test/README.md). Run them with `pio test -e simulator`; note that this overwrites the simulator's build output, so run `pio run -e simulator` afterward to restore the interactive binary before using it again.
