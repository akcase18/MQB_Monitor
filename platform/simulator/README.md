# platform/simulator/

A desktop stand-in for every [platform](../README.md) interface, built on raw SDL2. It backs **two products** — see [Environments](#environments) below. This is the primary development loop for GUI/layout work — no board, no car, no CAN bus needed. Deliberately does **not** use LVGL's own SDL driver (`lv_sdl_window_create()`/`lv_sdl_mouse_create()`), since that would register the display/input devices itself; instead `src/main.cpp` registers them uniformly across every platform via `IDisplay::flush()`/`ITouch::read()`, so the simulator and real hardware go through exactly the same LVGL wiring code.

## Files

- **`inc/simulator_display.hpp`** / **`src/simulator_display.cpp`** — `SimulatorDisplay`: an `SDL_Window` + `SDL_Renderer` + `SDL_Texture`, updated via `SDL_UpdateTexture` on `flush()`. SDL2 types are kept out of the header (opaque `void *` members) to match how vendor headers stay confined to `.cpp` files elsewhere in `platform/`.
- **`inc/simulator_touch.hpp`** / **`src/simulator_touch.cpp`** — `SimulatorTouch`: SDL2 mouse-button/position polling stands in for a touch panel. Presses in the button strip (below) are not reported as touches — the window is wider than the panel there, but LVGL only knows about the panel.
- **`inc/simulator_events.hpp`** / **`src/simulator_events.cpp`** — the one SDL event pump, plus the mouse and key state it produces. This used to live inside `SimulatorTouch::read()`, which worked only because touch happened to be polled every iteration; it moved out once buttons needed the same events, and because a buttonless-*and*-touchless configuration would otherwise leave the window unable to process its own close box.
- **`inc/simulator_buttons.hpp`** / **`src/simulator_buttons.cpp`** — `SimulatorButtons`: two on-screen buttons in a strip beside the emulated panel, pressed by mouse or by `a`/`b` (aliases `1`/`2`). Only built into a product that has buttons; otherwise `NullButtons` is wired in and the strip is not drawn at all. This class owns the button geometry as well as the input state, and `SimulatorDisplay` asks it where to draw — one definition of where a button is, shared by the thing that draws it and the thing that hit-tests it, so they cannot drift into a button you can see but not press.

  **Keyboard is not just a convenience:** one mouse cursor cannot press two buttons, so a chord (both at once) is only reachable from the keyboard, or a held key plus a click.
- **`inc/simulator_analog_input.hpp`** / **`src/simulator_analog_input.cpp`** — `SimulatorAnalogInput`: four channels of scripted engine warmup, matching the gauge product's planned ADS1115 assignment (oil pressure, oil temp, coolant temp, 5V rail sense).

  The waveforms are scripted in **engineering units** — psi and °C — then pushed through the *inverse* of each sensor's real transfer function to produce the voltage an ADC would see. That is deliberate: `core/sensors`' conditioning has to run the conversion forwards and land back on the number this file started from, which a simulator emitting convenient voltages directly would never catch. Verified round-trip error is 0.0000 psi / 0.0000 °C, with every channel between 0.353V and 3.012V across the full cycle.

  Coolant warms ahead of oil and settles cooler, which is the physically right ordering — a dashboard showing oil hotter than coolant on a cold start is a bug worth being able to see. Oil pressure falls as the oil thins and dips to idle pressure periodically, which is what makes the RPM-less low-pressure threshold problem visible on the desk rather than only in the car.

  Press **`f`** to cycle which channel fakes an **open circuit** (none → 0 → 1 → 2 → 3 → none). That case reports a real voltage and returns `true`, because a disconnected wire does not break the ADC — it produces a plausible number that only a valid-band check will reject. An open NTC leg reads the 3.3V pull-up, which converts to about −86°C. [core/sensors](../../core/sensors/README.md) now rejects both that and the transducer channel's 0V drift, so the panel blanks rather than showing either.

  Hold **`p`** to collapse oil pressure to a fault-level reading (~5 psi) for as long as the key is down. A *different* fault from `f`'s open circuit, and the distinction is the point: an open circuit is caught by the plausibility band and produces no reading at all, whereas this is a perfectly believable number for a genuinely bad pressure — which is what a warning threshold is for, and the only way to exercise [`core::WarningLatch`](../../core/vehicle/README.md)'s qualify/latch/acknowledge cycle without a car. A brief tap must not alarm; a sustained hold must latch and stay latched after release.

  It is applied in `read_microvolts()` rather than inside `microvolts_at()`, so that function stays a pure function of its arguments — a host test asserting the conditioning round trip must not depend on which keys happen to be held while it runs. The fault channel is passed in as an argument for the same reason; the pressure key is a live level with no natural argument, since nothing about it is worth replaying.

  The model is also exposed as pure statics — `microvolts_at(ch, t_ms, fault_channel, out)` plus `scripted_oil_pressure_psi()` and friends — because the round trip above is only worth claiming if something checks it. `test_core_sensors` drives those at chosen instants and asserts the recovered values match; reading the wall clock instead would make the assertion depend on when the test ran. `read_microvolts()` is those statics plus the clock and the fault key, nothing more.
- **`inc/simulator_config.hpp`** — which product the simulator is standing in for: panel size and whether the board has buttons or analog inputs, as build flags set per `[env:...]`. Defaults describe the MQB product, so an env that sets nothing behaves exactly as the simulator always has.
- **`inc/simulator_can.hpp`** / **`src/simulator_can.cpp`** — `SimulatorCanBus`: a synthetic `ICanBus`. `send()` immediately queues a fabricated UDS response for the next `receive()`, standing in for a real ECU so [core/can](../../core/can/README.md) and [core/vehicle](../../core/vehicle/README.md) (and the panels showing their data) can be exercised end-to-end without real hardware. Edit this file to change what fake data the simulator shows.

  Values are a per-signal base plus a slow triangular drift off a monotonic response counter — deterministic (no RNG, so a run reproduces exactly) but **not constant**. The drift isn't cosmetic: values used to be a pure function of the request bytes and therefore never changed, which meant [core/logging](../../core/logging/README.md)'s on-change recorder correctly wrote *nothing at all* and the whole logging path couldn't be exercised in the simulator. Because of the drift, a test can't recompute an expected value from a fixed formula — assert against `last_fake_raw()` instead (see `test/test_core_vehicle`).
- **`inc/simulator_clock.hpp`** / **`src/simulator_clock.cpp`** — `SimulatorClock`, wrapping `SDL_GetTicks()`.
- **`inc/simulator_config_store.hpp`** / **`src/simulator_config_store.cpp`** — `SimulatorConfigStore`, backed by a local file in the repo root instead of EEPROM: `sim_config.bin` for the MQB product, `sim_config_gauge.bin` for the gauge one.

  **One blob per product, for the same reason there is one window-position file per product.** Both write a `core::Config` with the same magic and version, but the struct's size and the meaning of every signal id inside it come from that product's own catalog. Sharing one file made the gauge simulator load the MQB product's panel assignment, resolve ids absent from its 3-signal catalog to −1, and silently blank the panels that got them — no error anywhere, just a missing gauge.

  This does **not** make real hardware safe. A physical device carrying a stale blob from the other product hits exactly the same problem; the fix there is a distinct `MAGIC_NUMBER` per product so the header is rejected rather than reinterpreted, which is the build plan's Phase 7.2. All this does is stop the desktop simulator staging that failure every time both products are run.
- **`inc/simulator_serial_link.hpp`** / **`src/simulator_serial_link.cpp`** — `SimulatorSerialLink`, backed by a real host serial port (not a stub) — see its `.cpp` for why.
- **`inc/simulator_storage.hpp`** / **`src/simulator_storage.cpp`** — `SimulatorStorage`, backed by a local directory (`sim_storage/` in the repo root) standing in for whichever real backend (`teensy/`'s onboard flash or Micro-SD) is active — one implementation is enough here since the medium doesn't matter to anything exercising the `IStorage` interface. It reports `StorageMedium::HostDirectory`, which is what the Logging screen shows as the storage label.
- **`src/simulator_platform.cpp`** — `platform_get()` for this environment.

## Environments

| Env | Panel | Input | Renders |
|---|---|---|---|
| `simulator` | 480x320 | touchscreen (mouse) | the MQB product, CAN-fed |
| `simulator_gauge` | 320x172 | two buttons | the gauge product, analog-fed |

They share every source file *here* and differ only in the flags `simulator_config.hpp`
reads. The MQB board has a touchscreen, so it gets no buttons; the gauge board has no
touchscreen, so buttons are the only way to interact with it.

Above `platform/` they differ in three ways, all of them stated in `platformio.ini` as
subtraction: `simulator_gauge` builds `src/main_gauge.cpp` instead of `src/main.cpp`,
compiles neither `core/can` nor `remote/` (no vehicle bus, no companion ESP32-C3), and
names the four `gui/screens` sources it wants rather than the directory. It is also the
only environment that compiles the gauge signal catalog, which is what keeps that catalog
honest.

**Two envs, not one that switches.** `env:simulator` keeps rendering the MQB product, so a
shared-code refactor stays regression-testable against the thing it must not change — one
command each proves both products still build and run.

**Keys:** `a`/`b` (or `1`/`2`) are the two buttons, and holding both is how a chord is
performed. `f` cycles the open-circuit fault channel. `p` holds oil pressure at a fault
level. All four are gauge-product concerns; on `env:simulator` only the mouse does
anything.

## Running

```sh
pio run -e simulator -t exec        # MQB product
pio run -e simulator_gauge -t exec  # gauge product, with buttons
```

Needs SDL2 installed on the host — see [support/README.md](../../support/README.md) for how the build locates it. `sim_config.bin` persists across runs the same way EEPROM does on real hardware; delete it to reset to defaults. `sim_storage/` similarly stands in for whichever real storage backend is active and persists the same way; delete it to reset. `sim_storage/`'s contents are git-ignored (`sim_config.bin` is still tracked) — see [sim_storage/README.md](../../sim_storage/README.md) for what the log chunk files in there are and how to decode them without hardware.
