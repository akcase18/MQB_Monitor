# platform/

Hardware abstraction: seven interfaces that everything in [core/](../core/README.md) and `src/main.cpp` talks to, and one implementation of each per supported board — except `ITouch` and `IStorage` on `teensy/`, which keep two implementations apiece (see below). Exactly one implementation of a given interface is wired into `Platform` for a given build, so nothing outside `platform/` ever needs an `#ifdef` to pick a board.

## Files

- **`platform.hpp`** — the interfaces:
  - `ICanBus` — `begin(tx_id, rx_id, baud)`, non-blocking `send(frame)`/`receive(out)`. Plus an opt-in sniffer mode -- `sniff_begin(baud, listen_only)` / `sniff_read(SniffFrame&)` / `sniff_send()` / `sniff_stats()` / `sniff_end()` -- that accepts every frame on the bus for `src/main.cpp`'s USB sniffer mode, then puts back exactly what `begin()` set up. Every hook has an "unsupported" default, so a board without it compiles unchanged; only `TeensyCanBus` implements it today.
  - `IDisplay` — `begin()`, `get_resolution()`, `flush()` (push an RGB565 pixel rect), `set_backlight(pct)`, `set_backlight_off()`. The last two are distinct on purpose: the user-facing 0–100% scale floors at a minimum duty (below ~15% the panel reads as black anyway), so `set_backlight(0)` means "as dim as it gets", not "off". `set_backlight_off()` is the true off, for hiding a transition the panel shouldn't be showing at all — see `src/main.cpp`'s `BacklightState`.
  - `ITouch` — `begin()`, non-blocking `read(out)`.
  - `IConfigStore` — raw byte-blob `read(offset, buf, len)`/`write(offset, buf, len)`/`capacity()`. [core/config](../core/config/README.md) owns the actual schema and versioning on top of this; this interface only knows about bytes.
  - `ISerialLink` — `begin(baud)`, non-blocking `read(buf, len)`/`write(buf, len)`. Raw byte transport to a companion board (the ESP32-C3 relay); `remote/protocol` owns framing/CRC on top of this.
  - `IStorage` — `begin()`/`is_present()`/`format()`, then stream-style `open(path, mode)`/`is_open()`/`close()`/`read()`/`write()`/`exists()`/`remove()`. One file open at a time. Deliberately medium-agnostic — see `teensy/` below for why there are two implementations of it. One informational query breaks that agnosticism on purpose, for the Logging screen's status readout: `medium()` (a `StorageMedium` — Flash/SdCard/HostDirectory), so the screen can say where logs are actually going. No capacity or free-space query is offered — the log ring's own budget is what bounds recording, and a free-space query would walk the filesystem on both real backends.
  - `IClock` — `millis()`, monotonic milliseconds since boot.
  - `Platform { ICanBus &can; IDisplay &display; ITouch &touch; IConfigStore &config_store; IClock &clock; ISerialLink &serial; IStorage &storage; }` and `Platform &platform_get()` — implemented once per board, returning that board's singleton instances.
  - Free functions each board implements alongside it: `platform_restart()`, plus the FIRMWARE hooks for field updates — `platform_firmware_version()`, `platform_update_path_ready()`, `platform_firmware_healthy()`, `platform_take_failed_update()` and `platform_hang_watchdog()`. Only [dash_s3/](dash_s3/README.md#firmware-updates) does anything with the FIRMWARE hooks; the other boards implement them as no-ops.

## Board-agnostic implementations

- **`synthetic_can_bus.hpp`** — `SyntheticCanBus`, an `ICanBus` that answers every UDS request instantly with a deterministic, slowly drifting fabricated value. Header-only, no dependencies. The simulator's only bus (`SimulatorCanBus` is a singleton wrapper), and any real board can swap it in for its hardware CAN driver to run the display pipeline on a bench with no vehicle — `env:dash_s3_stress` and `env:teensymm_stress` do exactly that (sweeping faster and wider than the simulator's gentle wobble, via the constructor arguments).

## Boards

- **[teensy/](teensy/README.md)** — the real hardware: FlexCAN_T4, ILI948x_t4_mm, Teensy `EEPROM.h`, and (uniquely) two switchable implementations each of `ITouch` (capacitive FT6236 over I2C, or resistive XPT2046 over SPI) and `IStorage` (internal MCU program flash via `LittleFS_Program`, or Micro-SD via SdFat).
- **[simulator/](simulator/README.md)** — a desktop stand-in over raw SDL2, with a file-backed config store and a synthetic CAN bus, used as the primary GUI development loop.
- **[esp32_common/](esp32_common/README.md)** — the MCU-level half of any ESP32-S3 board: `IClock` over `millis()`, `IConfigStore` over the NVS-backed EEPROM emulation, `IStorage` over LittleFS. No pin numbers.
- **[dash_s3/](dash_s3/README.md)** — the dash on an ESP32-S3 (DevKitC-1 N16R8 on the bench today): the same ILI9488 panel over the S3's 8-bit i80 bus via `esp_lcd`, the same FT6236, TWAI for CAN, and a UART to the C3 relay. Composes `esp32_common/` with its own board-level drivers. Not yet run on hardware.
- **[esp32/](esp32/README.md)** — the companion ESP32-C3 relay firmware for wireless configuration. Not another `Platform` implementation (no display/touch/CAN on that board) — see its own README for what it actually is.

## Adding a new board

Create `platform/<board>/{inc,src}/`, implement the interfaces (one class each, matching the existing boards' `<Board>CanBus`/`<Board>Display`/`<Board>Touch`/`<Board>ConfigStore`/`<Board>Clock`/`<Board>SerialLink`/`<Board>Storage` naming), implement `platform_get()` returning a `Platform` built from those singletons (plus `platform_restart()` and the FIRMWARE hooks — no-ops are fine), and add a new `[env:<board>]` to `platformio.ini` with a `build_src_filter` that includes only that board's `platform/<board>/src` (and excludes every other board's). Nothing in `core/` or `gui/` needs to change.
