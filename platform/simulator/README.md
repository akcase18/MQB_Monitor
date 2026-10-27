# platform/simulator/

A desktop stand-in for every [platform](../README.md) interface, built on raw SDL2. This is the primary development loop for GUI/layout work — no board, no car, no CAN bus needed. Deliberately does **not** use LVGL's own SDL driver (`lv_sdl_window_create()`/`lv_sdl_mouse_create()`), since that would register the display/input devices itself; instead `src/main.cpp` registers them uniformly across every platform via `IDisplay::flush()`/`ITouch::read()`, so the simulator and real hardware go through exactly the same LVGL wiring code.

## Files

- **`inc/simulator_display.hpp`** / **`src/simulator_display.cpp`** — `SimulatorDisplay`: an `SDL_Window` + `SDL_Renderer` + `SDL_Texture`, updated via `SDL_UpdateTexture` on `flush()`. SDL2 types are kept out of the header (opaque `void *` members) to match how vendor headers stay confined to `.cpp` files elsewhere in `platform/`.
- **`inc/simulator_touch.hpp`** / **`src/simulator_touch.cpp`** — `SimulatorTouch`: SDL2 mouse-button/position polling stands in for a touch panel. Also pumps the SDL event queue (window close, the relay's keys) as a side effect of `read()`, since that's the one function called every LVGL tick.
- **`inc/simulator_can.hpp`** / **`src/simulator_can.cpp`** — `SimulatorCanBus`: a synthetic `ICanBus`. `send()` immediately queues a fabricated UDS response for the next `receive()`, standing in for a real ECU so [core/can](../../core/can/README.md) and [core/vehicle](../../core/vehicle/README.md) (and the panels showing their data) can be exercised end-to-end without real hardware. Edit this file to change what fake data the simulator shows.

  Values are a per-signal base plus a slow triangular drift off a monotonic response counter — deterministic (no RNG, so a run reproduces exactly) but **not constant**. The drift isn't cosmetic: values used to be a pure function of the request bytes and therefore never changed, which meant [core/logging](../../core/logging/README.md)'s on-change recorder correctly wrote *nothing at all* and the whole logging path couldn't be exercised in the simulator. Because of the drift, a test can't recompute an expected value from a fixed formula — assert against `last_fake_raw()` instead (see `test/test_core_vehicle`).
- **`inc/simulator_clock.hpp`** / **`src/simulator_clock.cpp`** — `SimulatorClock`, wrapping `SDL_GetTicks()`.
- **`inc/simulator_config_store.hpp`** / **`src/simulator_config_store.cpp`** — `SimulatorConfigStore`, backed by a local file (`sim_config.bin` in the repo root) instead of EEPROM.
- **`inc/simulator_relay.hpp`** / **`src/simulator_relay.cpp`** — `SimulatorRelay`, the `ISerialLink`: an in-process stand-in for the dash's Wi-Fi relay, so the Wi-Fi and firmware-update screens work with no phone and no board. See [below](#wi-fi-and-firmware-update-screens).
- **`inc/simulator_storage.hpp`** / **`src/simulator_storage.cpp`** — `SimulatorStorage`, backed by a local directory (`sim_storage/` in the repo root) standing in for whichever real backend (`teensy/`'s onboard flash or Micro-SD) is active — one implementation is enough here since the medium doesn't matter to anything exercising the `IStorage` interface. It reports `StorageMedium::HostDirectory`, which is what the Logging screen shows as the storage label.
- **`src/simulator_platform.cpp`** — `platform_get()` for this environment.

## Running

```sh
pio run -e simulator -t exec
```

Needs SDL2 installed on the host — see [support/README.md](../../support/README.md) for how the build locates it. `sim_config.bin` persists across runs the same way EEPROM does on real hardware; delete it to reset to defaults. `sim_storage/` similarly stands in for whichever real storage backend is active and persists the same way; delete it to reset. `sim_storage/`'s contents are git-ignored (`sim_config.bin` is still tracked) — see [sim_storage/README.md](../../sim_storage/README.md) for what the log chunk files in there are and how to decode them without hardware.

## Wi-Fi and firmware-update screens

`SimulatorRelay` answers the dash the way the S3's relay task does. Settings → Wi-Fi brings the "AP" up after a moment, so the QR screen shows. Keys in the simulator window fake a phone joining it and using the update page:

| Key | Does |
| --- | --- |
| **P** | A phone joins the AP, or leaves it: the Wi-Fi glyph goes from grey to the accent and back. Leaving while the dashboard is up goes back to the QR code. Only while the AP is up. |
| **V** | The phone opens the config page: the dash's own config is previewed unchanged, with sample values, so the dashboard shows the **Tap screen to exit preview** banner. Joins first, if **P** hasn't. |
| **H** | The page goes away (switched from, phone locked, closed), as the real page's beacon reports it: back to the QR code. |
| **D** | The page's **Discard**: the preview ends and a fresh one starts, staying on the dashboard. |
| **U** | The phone asks for update mode, and the **Update firmware?** popup comes up. Joins first, if **P** hasn't. Only while the AP is up, as on the dash. |
| **S** | Sends a firmware (`v9.9.9-sim`, ~6 s of progress), once **Start** has been tapped. **Install and restart?** follows. |
| **F** | Sends one that fails halfway, back to waiting. |

Install shows **Restarting...** and then restarts the simulator. Nothing is installed. The fake page edits nothing; it's there for the dash's side of a session. The page itself is only testable against a real dash.
