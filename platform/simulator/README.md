# platform/simulator/

A desktop stand-in for every [platform](../README.md) interface, built on raw SDL2. This is the primary development loop for GUI/layout work — no board, no car, no CAN bus needed. Deliberately does **not** use LVGL's own SDL driver (`lv_sdl_window_create()`/`lv_sdl_mouse_create()`), since that would register the display/input devices itself; instead `src/main.cpp` registers them uniformly across every platform via `IDisplay::flush()`/`ITouch::read()`, so the simulator and real hardware go through exactly the same LVGL wiring code.

## Files

- **`inc/simulator_display.hpp`** / **`src/simulator_display.cpp`** — `SimulatorDisplay`: an `SDL_Window` + `SDL_Renderer` + `SDL_Texture`, updated via `SDL_UpdateTexture` on `flush()`. SDL2 types are kept out of the header (opaque `void *` members) to match how vendor headers stay confined to `.cpp` files elsewhere in `platform/`.
- **`inc/simulator_touch.hpp`** / **`src/simulator_touch.cpp`** — `SimulatorTouch`: SDL2 mouse-button/position polling stands in for a touch panel. Also pumps the SDL event queue (window close, etc.) as a side effect of `read()`, since that's the one function called every LVGL tick.
- **`inc/simulator_can.hpp`** / **`src/simulator_can.cpp`** — `SimulatorCanBus`: a synthetic `ICanBus`. `send()` immediately queues a fabricated UDS response for the next `receive()`, standing in for a real ECU so [core/can](../../core/can/README.md) and [core/vehicle](../../core/vehicle/README.md) (and the panels showing their data) can be exercised end-to-end without real hardware. Edit this file to change what fake data the simulator shows.
- **`inc/simulator_clock.hpp`** / **`src/simulator_clock.cpp`** — `SimulatorClock`, wrapping `SDL_GetTicks()`.
- **`inc/simulator_config_store.hpp`** / **`src/simulator_config_store.cpp`** — `SimulatorConfigStore`, backed by a local file (`sim_config.bin` in the repo root) instead of EEPROM.
- **`inc/simulator_serial_link.hpp`** / **`src/simulator_serial_link.cpp`** — `SimulatorSerialLink`, backed by a real host serial port (not a stub) — see its `.cpp` for why.
- **`inc/simulator_storage.hpp`** / **`src/simulator_storage.cpp`** — `SimulatorStorage`, backed by a local directory (`sim_storage/` in the repo root) standing in for whichever real backend (`teensy/`'s onboard flash or Micro-SD) is active — one implementation is enough here since the medium doesn't matter to anything exercising the `IStorage` interface.
- **`src/simulator_platform.cpp`** — `platform_get()` for this environment.

## Running

```sh
pio run -e simulator -t exec
```

Needs SDL2 installed on the host — see [support/README.md](../../support/README.md) for how the build locates it. `sim_config.bin` persists across runs the same way EEPROM does on real hardware; delete it to reset to defaults. `sim_storage/` similarly stands in for whichever real storage backend is active and persists the same way; delete it to reset.
