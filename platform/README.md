# platform/

Hardware abstraction: nine interfaces that everything in [core/](../core/README.md) and the composition roots talk to, and one implementation of each per supported board — except `ITouch` and `IStorage` on `teensy/`, which keep two implementations apiece (see below). Exactly one implementation of a given interface is wired into `Platform` for a given build, so nothing outside `platform/` ever needs an `#ifdef` to pick a board.

## Null implementations

`Platform`'s members are references, so every board must supply one of everything even when the hardware isn't there. Five header-only nulls live directly in this directory rather than under any board, because "this board has no CAN bus" is not a property of a particular board:

| | For a board with… |
|---|---|
| `null_can_bus.hpp` | no vehicle bus. `begin()` reports failure and `send()` refuses, rather than silently behaving like an ECU that never answers. |
| `null_touch.hpp` | no touchscreen. **A board wiring this must also pass `with_pointer_indev: false`** to `app::lvgl_runtime::begin()` — an `lv_indev` over it would poll forever and never produce an event. |
| `null_buttons.hpp` | no buttons. `count()` is 0, so a caller that iterates buttons does nothing. |
| `null_analog_input.hpp` | no ADC. |
| `null_serial_link.hpp` | no companion processor. Writes report **0** bytes sent, not `len` — telling a caller its frame went out when nothing happened is worse than failing. |

The gauge board uses three of them; the Teensy uses two. What a board *doesn't* have is as much a part of its description as what it does.

## Files

- **`platform.hpp`** — the interfaces:
  - `ICanBus` — `begin(tx_id, rx_id, baud)`, non-blocking `send(frame)`/`receive(out)`.
  - `IDisplay` — `begin()`, `get_resolution()`, `flush()` (push an RGB565 pixel rect), `set_backlight(pct)`, `set_backlight_off()`. The last two are distinct on purpose: the user-facing 0–100% scale floors at a minimum duty (below ~15% the panel reads as black anyway), so `set_backlight(0)` means "as dim as it gets", not "off". `set_backlight_off()` is the true off, for hiding a transition the panel shouldn't be showing at all — see `src/main.cpp`'s `BacklightState`.
  - `ITouch` — `begin()`, non-blocking `read(out)`.
  - `IButtons` — `begin()`, `count()`, `is_down(idx)`. Momentary push buttons by index, raw level only: no debounce, no press-duration classification. That belongs in `core/input`, where it is host-testable against a synthetic clock rather than a finger and a stopwatch. A board with no buttons supplies `NullButtons` (`platform/null_buttons.hpp`, header-only), whose `count()` is 0 — which is what the Teensy board uses, since it has a touchscreen instead.
  - `IAnalogInput` — `begin()`, `channel_count()`, `read_microvolts(ch, out)`. A multi-channel ADC reporting measured voltage at the pin and nothing else — divider ratios, sensor transfer functions, thermistor curves, filtering and plausibility limits are all `core/sensors`' job, where they are plain math over plain numbers and host-testable. Microvolts rather than millivolts because an ADS1115 at its ±4.096V range resolves 125µV per LSB. A failed read returns false and is **not** the same as a valid reading of 0V: 0V is a plausible sensor output, a failure means the value is unknown. Boards with no ADC supply `NullAnalogInput` (`platform/null_analog_input.hpp`).
  - `IConfigStore` — raw byte-blob `read(offset, buf, len)`/`write(offset, buf, len)`/`capacity()`. [core/config](../core/config/README.md) owns the actual schema and versioning on top of this; this interface only knows about bytes.
  - `ISerialLink` — `begin(baud)`, non-blocking `read(buf, len)`/`write(buf, len)`. Raw byte transport to a companion board (the ESP32-C3 relay); `remote/protocol` owns framing/CRC on top of this.
  - `IStorage` — `begin()`/`is_present()`/`format()`, then stream-style `open(path, mode)`/`is_open()`/`close()`/`read()`/`write()`/`exists()`/`remove()`. One file open at a time. Deliberately medium-agnostic — see `teensy/` below for why there are two implementations of it. One informational query breaks that agnosticism on purpose, for the Logging screen's status readout: `medium()` (a `StorageMedium` — Flash/SdCard/HostDirectory), so the screen can say where logs are actually going. No capacity or free-space query is offered — the log ring's own budget is what bounds recording, and a free-space query would walk the filesystem on both real backends.
  - `IClock` — `millis()`, monotonic milliseconds since boot.
  - `Platform { ICanBus &can; IDisplay &display; ITouch &touch; IButtons &buttons; IAnalogInput &analog; IConfigStore &config_store; IClock &clock; ISerialLink &serial; IStorage &storage; }` and `Platform &platform_get()` — implemented once per board, returning that board's singleton instances.

## Boards

- **[teensy/](teensy/README.md)** — the real hardware: FlexCAN_T4, ILI948x_t4_mm, Teensy `EEPROM.h`, and (uniquely) two switchable implementations each of `ITouch` (capacitive FT6236 over I2C, or resistive XPT2046 over SPI) and `IStorage` (internal MCU program flash via `LittleFS_Program`, or Micro-SD via SdFat).
- **[simulator/](simulator/README.md)** — a desktop stand-in over raw SDL2, with a file-backed config store and a synthetic CAN bus, used as the primary GUI development loop.
- **[esp32s3/](esp32s3/README.md)** — the analog gauge product: ST7789V2 320×172 over SPI, an ADS1115 over I²C, two GPIO buttons, NVS-backed config and LittleFS. **Never run on hardware** — it builds and links and that is the whole extent of what is known; see its README for the three bring-up risks. Pairs with `src/main_gauge.cpp`, and adding it required no change anywhere above `platform/`.
- **[esp32/](esp32/README.md)** — the companion ESP32-**C3** relay firmware for wireless configuration, for the *CAN* product. Not another `Platform` implementation (no display/touch/CAN on that board) — see its own README for what it actually is. Not to be confused with `esp32s3/` above, which is a real board layer for a different product.

## Adding a new board

Create `platform/<board>/{inc,src}/`, implement the interfaces (one class each, matching the existing boards' `<Board>CanBus`/`<Board>Display`/`<Board>Touch`/`<Board>ConfigStore`/`<Board>Clock`/`<Board>SerialLink`/`<Board>Storage` naming), reach for the nulls above for whatever the board does not have, implement `platform_get()` returning a `Platform` built from those singletons, and add a new `[env:<board>]` to `platformio.ini` naming only that board's `platform/<board>/src`. Nothing in `core/` or `gui/` needs to change — `esp32s3/` is the worked example, and it added no line anywhere above this directory.

Two things that bit the S3 and will bite the next board:

- **`setup()`/`loop()` linkage is per-core.** Teensyduino declares them `extern "C"`; Arduino-ESP32 declares them as plain C++. `src/app/entry_point.hpp` branches on the toolchain's own defines. Getting it wrong fails only in the firmware link, and the error reads like a missing file rather than a linkage problem.
- **Put the pinout in one header, every pin a `-D`.** See `esp32s3/inc/esp32s3_pins.hpp`. A pin number inside a driver is a pin number somebody has to go find when the PCB changes.
