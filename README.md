# MQB Monitor

A fully configurable data-monitoring dashboard for MQB-platform (VW/Audi group) vehicles. It polls live engine data over CAN/UDS and shows it on a 3.5" touchscreen mounted in the car — up to 6 signals at once, each independently configurable, refreshing several times a second.

Built on a Teensy MicroMod, [LVGL](https://lvgl.io/) for the UI, and a from-scratch UDS/CAN stack. See [ARCHITECTURE.md](ARCHITECTURE.md) for how the code is organized and where to make changes.

## Features

- **Live engine data** — coolant/oil/intake air temp, boost/ambient pressure, RPM, torque, throttle position, speed, fuel level, per-cylinder misfire counts and timing correction, and more. The full signal list lives in [core/vehicle](core/vehicle/README.md).
- **Configurable panels** — tap any panel to reassign it to a different signal, switch it between Metric/Imperial, and choose how it's drawn:
  - **Digital** — bare number.
  - **Bar** — number plus a fill bar scaled to the signal's expected range.
  - **Gauge** — number plus a radial arc.
  
  A graphic is never shown without its number — Bar/Gauge add a fast-glance visual, they don't replace the reading.
- **1–6 panel layouts** — each panel count (1, 2, 3, 4, 5, 6) gets its own screen shape rather than a generic grid, tuned for readability at arm's length in a car. Horizontal orientation is fully shaped per-count; vertical currently falls back to a generic transpose.
- **Per-signal warning thresholds** — set independent low/high warning bounds per signal; a panel showing a signal outside its bounds highlights immediately.
- **Day/Night brightness** — a single toggle switches between two independently-set backlight levels (defaults: 100% day / 20% night) and applies instantly, so it's usable mid-drive. There's no automatic (clock- or sensor-based) switching yet — see [TODO.md](TODO.md).
- **Theming** — accent color is user-selectable from a preset swatch grid and persists across restarts; the whole app can be reskinned by editing one file ([gui/theme](gui/theme/README.md)), which the codebase proves out with two distinct built-in themes.
- **Desktop simulator** — the entire GUI runs on a laptop (SDL2 window, mouse-as-touch, fabricated CAN responses), so screen/layout work doesn't require the car or the physical board. See [platform/simulator](platform/simulator/README.md).

## Hardware

- **MCU**: Teensy MicroMod (`teensymm`), Arduino framework.
- **Display**: ILI948x-family 480x320 TFT via the `ILI948x_t4_mm` library.
- **Touch**: resistive touch panel over SPI (XPT2046-style protocol).
- **CAN**: `FlexCAN_T4`, polling the vehicle's OBD2 UDS enhanced-diagnostic-session service.
- **Storage**: onboard EEPROM for persisted settings/panel/warning configuration.
- **PCB**: custom board (v2), designed in-house.

## Current State

Running in the car and working well — CAN polling, panel display, warning thresholds, and settings all confirmed on real hardware and real vehicle data.

Outstanding hardware work:
- BOOT and RESET buttons are still not populated on the PCB (provisions exist, buttons just need to be ordered/installed).
- The board/enclosure needs a physical redesign to be smaller and more heat-resistant for permanent in-car mounting.

Software TODO list, including deferred features (automatic day/night switching, additional signal sources, data logging, etc.), is tracked in [TODO.md](TODO.md).

## Building

This is a [PlatformIO](https://platformio.org/) project with two build environments:

```sh
# Desktop simulator (SDL2 window, no hardware needed) — the primary dev loop for GUI work
pio run -e simulator -t exec

# Real hardware
pio run -e teensymm -t upload
```

Run the host-buildable unit test suite (signal/config/layout logic — no hardware, no LVGL rendering) with:

```sh
pio test -e simulator
```

The simulator env needs SDL2 installed (Homebrew on macOS, MSYS2 on Windows) — see [support/README.md](support/README.md) for how that's located.

## Repository Layout

```
platform/   hardware abstraction (CAN, display, touch, config storage, clock) — one implementation per board
core/       platform-agnostic vehicle domain logic (scheduling, UDS, signal catalog, config schema)
gui/        LVGL presentation layer (theme, widgets, screens, and the one bridge into core/)
src/        composition root — wires platform/core/gui together per build environment
support/    PlatformIO build-script helpers (SDL2 discovery, simulator execute target)
test/       host-buildable unit tests
lib/        vendored/third-party libraries (LVGL, FlexCAN_T4, ILI948x_t4_mm)
```

Full explanation of the dependency rules between these, and a guide to where to make common changes, is in [ARCHITECTURE.md](ARCHITECTURE.md).

## Fonts

UI fonts are JetBrains Mono (SIL OFL 1.1) and, for the Day/Night toggle's sun/moon glyphs, Font Awesome Free Solid (SIL OFL 1.1). Licenses are in [gui/assets/font-licenses/](gui/assets/font-licenses/).
