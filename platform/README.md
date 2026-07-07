# platform/

Hardware abstraction: five interfaces that everything in [core/](../core/README.md) and `src/main.cpp` talks to, and one implementation of all five per supported board. Exactly one implementation is linked into a given build (`platformio.ini`'s per-env `build_src_filter`), so nothing outside `platform/` ever needs an `#ifdef` to pick a board.

## Files

- **`platform.hpp`** — the interfaces:
  - `ICanBus` — `begin(tx_id, rx_id, baud)`, non-blocking `send(frame)`/`receive(out)`.
  - `IDisplay` — `begin()`, `get_resolution()`, `flush()` (push an RGB565 pixel rect), `flush_ready()`, `set_backlight(pct)`.
  - `ITouch` — `begin()`, non-blocking `read(out)`.
  - `IConfigStore` — raw byte-blob `read(offset, buf, len)`/`write(offset, buf, len)`/`capacity()`. [core/config](../core/config/README.md) owns the actual schema and versioning on top of this; this interface only knows about bytes.
  - `IClock` — `millis()`, monotonic milliseconds since boot.
  - `Platform { ICanBus &can; IDisplay &display; ITouch &touch; IConfigStore &config_store; IClock &clock; }` and `Platform &platform_get()` — implemented once per board, returning that board's singleton instances.

## Boards

- **[teensy/](teensy/README.md)** — the real hardware: FlexCAN_T4, ILI948x_t4_mm, an SPI touch driver, and Teensy `EEPROM.h`.
- **[simulator/](simulator/README.md)** — a desktop stand-in over raw SDL2, with a file-backed config store and a synthetic CAN bus, used as the primary GUI development loop.
- **esp32/** — not implemented yet. The interfaces above were designed with a future ESP32 port in mind (TWAI for CAN, NVS for config storage) but no code exists for it — see [TODO.md](../TODO.md).

## Adding a new board

Create `platform/<board>/{inc,src}/`, implement all five interfaces (one class each, matching the existing boards' `<Board>CanBus`/`<Board>Display`/`<Board>Touch`/`<Board>ConfigStore`/`<Board>Clock` naming), implement `platform_get()` returning a `Platform` built from those singletons, and add a new `[env:<board>]` to `platformio.ini` with a `build_src_filter` that includes only that board's `platform/<board>/src` (and excludes every other board's). Nothing in `core/` or `gui/` needs to change.
