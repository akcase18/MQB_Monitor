# platform/

Hardware abstraction: seven interfaces that everything in [core/](../core/README.md) and `src/main.cpp` talks to, and one implementation of each per supported board — except `ITouch` and `IStorage` on `teensy/`, which keep two implementations apiece (see below). Exactly one implementation of a given interface is wired into `Platform` for a given build, so nothing outside `platform/` ever needs an `#ifdef` to pick a board.

## Files

- **`platform.hpp`** — the interfaces:
  - `ICanBus` — `begin(tx_id, rx_id, baud)`, non-blocking `send(frame)`/`receive(out)`.
  - `IDisplay` — `begin()`, `get_resolution()`, `flush()` (push an RGB565 pixel rect), `set_backlight(pct)`.
  - `ITouch` — `begin()`, non-blocking `read(out)`.
  - `IConfigStore` — raw byte-blob `read(offset, buf, len)`/`write(offset, buf, len)`/`capacity()`. [core/config](../core/config/README.md) owns the actual schema and versioning on top of this; this interface only knows about bytes.
  - `ISerialLink` — `begin(baud)`, non-blocking `read(buf, len)`/`write(buf, len)`. Raw byte transport to a companion board (the ESP32-C3 relay); `remote/protocol` owns framing/CRC on top of this.
  - `IStorage` — `begin()`/`is_present()`/`format()`, then stream-style `open(path, mode)`/`is_open()`/`close()`/`read()`/`write()`/`exists()`/`remove()`. One file open at a time. Deliberately medium-agnostic — see `teensy/` below for why there are two implementations of it.
  - `IClock` — `millis()`, monotonic milliseconds since boot.
  - `Platform { ICanBus &can; IDisplay &display; ITouch &touch; IConfigStore &config_store; IClock &clock; ISerialLink &serial; IStorage &storage; }` and `Platform &platform_get()` — implemented once per board, returning that board's singleton instances.

## Boards

- **[teensy/](teensy/README.md)** — the real hardware: FlexCAN_T4, ILI948x_t4_mm, Teensy `EEPROM.h`, and (uniquely) two switchable implementations each of `ITouch` (capacitive FT6236 over I2C, or resistive XPT2046 over SPI) and `IStorage` (internal MCU program flash via `LittleFS_Program`, or Micro-SD via SdFat).
- **[simulator/](simulator/README.md)** — a desktop stand-in over raw SDL2, with a file-backed config store and a synthetic CAN bus, used as the primary GUI development loop.
- **[esp32/](esp32/README.md)** — the companion ESP32-C3 relay firmware for wireless configuration. Not another `Platform` implementation (no display/touch/CAN on that board) — see its own README for what it actually is.

## Adding a new board

Create `platform/<board>/{inc,src}/`, implement the interfaces (one class each, matching the existing boards' `<Board>CanBus`/`<Board>Display`/`<Board>Touch`/`<Board>ConfigStore`/`<Board>Clock`/`<Board>SerialLink`/`<Board>Storage` naming), implement `platform_get()` returning a `Platform` built from those singletons, and add a new `[env:<board>]` to `platformio.ini` with a `build_src_filter` that includes only that board's `platform/<board>/src` (and excludes every other board's). Nothing in `core/` or `gui/` needs to change.
