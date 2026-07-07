# platform/teensy/

The real-hardware implementation of every [platform](../README.md) interface, for the Teensy MicroMod board this project runs on in the car.

## Files

- **`inc/teensy_can.hpp`** / **`src/teensy_can.cpp`** — `TeensyCanBus`, wrapping `lib/FlexCAN_T4` (mailbox filter, baud, mailbox setup for the OBD2 response ID).
- **`inc/teensy_display.hpp`** / **`src/teensy_display.cpp`** — `TeensyDisplay`, wrapping `lib/ILI948x_t4_mm` for the 480×320 ILI948x-family panel (init, pixel flush, backlight PWM).
- **`inc/teensy_touch.hpp`** / **`src/teensy_touch.cpp`** — `TeensyTouch`, a raw SPI XPT2046-style touch controller driver (multi-sample averaging + rotation remap), ported directly from the original firmware's touch driver.
- **`inc/teensy_clock.hpp`** / **`src/teensy_clock.cpp`** — `TeensyClock`, wrapping Arduino's `millis()`.
- **`inc/teensy_config_store.hpp`** / **`src/teensy_config_store.cpp`** — `TeensyConfigStore`, wrapping Teensy's `EEPROM.h` for persisted config storage.
- **`src/teensy_platform.cpp`** — `platform_get()` for this board: builds the `Platform` struct from the five singletons above.

Each class is a lazily-constructed Meyer's singleton (`instance()`), matching the pattern `src/main.cpp`'s composition root itself uses.

## Building/flashing

```sh
pio run -e teensymm -t upload
```
