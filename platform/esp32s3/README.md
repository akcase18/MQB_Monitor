# platform/esp32s3/

The analog gauge product's board layer: an ESP32-S3 driving an ST7789V2 320×172 SPI strip, three analog senders through an ADS1115, and two buttons.

Built by `[env:esp32s3_gauge]`, which pairs it with `src/main_gauge.cpp` and `core/vehicle/generated/gauge/` — the same composition root and the same catalog `env:simulator_gauge` uses. **Nothing above `platform/` was edited to accept a real board.** That is the whole payoff of the layering, and it is worth stating plainly: this directory is additive.

Not to be confused with **[esp32/](../esp32/README.md)**, which is the ESP32-**C3** Wi-Fi relay for the *other* product and is not a `Platform` implementation at all.

## ⚠ None of this has run on hardware

The board does not exist yet. Everything here builds, links, and has every symbol in the image — and that is the entire extent of what is known. Three things in particular are the classic ways an ST7789/ADS1115 bring-up goes wrong, and each is documented next to the code it constrains rather than here:

| Risk | Where | What it looks like |
|---|---|---|
| GRAM offset / MADCTL rotation | `esp32s3_display.cpp` | Garbage stripe down one edge, everything shifted by 34 px |
| RGB565 byte order | same | Believable but wrong colours — easy to stare past |
| ADS1115 timing and noise | `esp32s3_analog_input.cpp` | Readings that are plausible and wrong, which is the one failure this product cannot tolerate |

The desktop simulator renders LVGL's own output and knows nothing about any of them. A clean `env:simulator_gauge` run says exactly nothing about this directory.

## The pinout is not frozen

Every pin is a `-D` with a default, in **`inc/esp32s3_pins.hpp`**. Override them from `platformio.ini` when the PCB settles; do not edit a driver.

The defaults avoid the S3's traps — strapping pins (0, 3, 45, 46), native USB (19, 20), the flash bus (26–32), the octal PSRAM bus on an R8 module (33–37), and UART0 (43, 44) — which leaves everything on 1–18 and 21.

| | Pin | |
|---|---|---|
| LCD SCLK / MOSI / CS | 12 / 11 / 10 | FSPI. Write-only, so no MISO. |
| LCD DC / RST / BL | 9 / 8 / 21 | BL is LEDC PWM, so brightness means something. |
| I²C SDA / SCL | 4 / 5 | ADS1115 at 0x48. Two pins is why the ADC is not on SPI: the display keeps that bus to itself. |
| Button A / B | 6 / 7 | Active-low against internal pull-ups. |
| RPM | 15 | **Reserved and unused.** See below. |

**GPIO15 is reserved on purpose.** Without engine speed there is no honest low-oil-pressure threshold — one high enough to catch real loss at 3000 rpm alarms at every stoplight. The ESP32's PCNT peripheral counts pulses on any GPIO at no ADC cost, so reserving the pin now is nearly free and retrofitting it after layout is not.

## Files

- **`inc/esp32s3_pins.hpp`** — every pin, overridable. Start here.
- **`inc/esp32s3_display.hpp`** / **`src/esp32s3_display.cpp`** — `Esp32s3Display`, an ST7789V2 driver written directly against Arduino's `SPIClass` rather than pulling in a display library. The driver *is* an init sequence, a window-set and a pixel push; the two things that make this panel different from a generic ST7789 are both things a library would hide behind a configuration flag that is easy to get wrong.

  Three details worth knowing before touching it:
  - **The GRAM offset is 34.** An ST7789 has 240×320 of frame memory; a 172-pixel-wide panel is wired to the *middle* of the 240 axis, so every address shifts by `(240 − 172) / 2`. In landscape that axis is the rows, so the offset lands on Y.
  - **CS stays low across a command and its parameters.** Raising it in between ends the command as far as the controller is concerned, and the parameters that follow are read as a fresh command. The failure is a panel that shows nothing at all with every byte visibly correct on a logic analyser — which is why `write_command()` takes the parameters rather than there being a separate `write_data()` to forget to pair with it.
  - **The byte swap is in `flush()`, not in LVGL.** LVGL renders RGB565 little-endian; the ST7789 reads big-endian. `IDisplay::flush()` takes a `const uint16_t *` because the buffer belongs to LVGL, so the swap goes into a 256-pixel stack chunk on the way out.

  The backlight comes up **off** and stays off until something has been rendered — the same promise the Teensy driver makes, so power-on garbage in frame memory is never visible. `app::backlight` owns when it lights.

- **`inc/esp32s3_analog_input.hpp`** / **`src/esp32s3_analog_input.cpp`** — `Esp32s3AnalogInput` over an ADS1115. Single-shot conversions at 860 SPS, ±4.096 V range (125 µV/LSB), comparator disabled.

  **Not the S3's own ADC**, deliberately: ADC2 is unusable whenever Wi-Fi is active and on-die Wi-Fi is this product's premise, and the internal SAR is noisy and non-linear near both rails with error that correlates with radio activity — the worst possible shape for a fault nobody is looking for.

  **It blocks for ~1.2 ms per read**, which is deliberate rather than overlooked: `core::AnalogSignalSource` samples one channel per slot, so a loop pass spends 1.2 ms here and the rest on LVGL. Sweeping all four per call would cost 5 ms and drop a frame. If that stops being acceptable the fix is to start a conversion on one pass and collect it on the next; the round-robin structure above already suits it.

- **`inc/esp32s3_buttons.hpp`** / **`src/esp32s3_buttons.cpp`** — `Esp32s3Buttons`. Raw level only; debounce, gestures and the chord all live in [core/input](../../core/input/README.md) where they are host-testable against a synthetic clock rather than a finger and a stopwatch. `ESP32S3_BUTTON_COUNT` drops to 1 for a board with only one switch fitted — `core::ActionMap::set_defaults()` re-shapes itself, so that is a BOM decision rather than a firmware one.

- **`inc/esp32s3_config_store.hpp`** / **`src/esp32s3_config_store.cpp`** — `Esp32s3ConfigStore` over the Arduino EEPROM emulation (NVS-backed), 4 KB. Holds both `core::Config` and `core::DeviceConfig` (see [core/device_config](../../core/device_config/README.md)).

  **Every write commits.** `IConfigStore` has no flush, so a caller is entitled to assume durability on return. NVS rewrites the whole region on any commit, so the partial writers (`save_priority`, `save_sensor`) save bus traffic here rather than flash wear. Fine at the rate config actually changes; not fine if something ever writes config from a loop.

  It opens itself on first use, because `IConfigStore` has no `begin()` — the Teensy's real EEPROM needs none, and adding one to the interface for this board alone would give every other implementation an empty method to carry.

- **`inc/esp32s3_storage.hpp`** / **`src/esp32s3_storage.cpp`** — `Esp32s3Storage` over LittleFS, holding `core::Logger`'s chunk ring. Config deliberately lives elsewhere (NVS), so erasing the logs cannot take the calibration with it.

- **`src/esp32s3_platform.cpp`** — `platform_get()`. **Three of the nine members are nulls**, and that is the honest description of this hardware rather than a gap: no CAN (three analog sensors replace the bus — the product's premise), no touch (two buttons and one screen), no serial link (Wi-Fi is on-die, so there is no companion processor and no wire to it).

- **`partitions_gauge.csv`** — 16 MB: 20 KB NVS, two 4 MB app slots (OTA-capable), 7.94 MB LittleFS. The app slots are generous on purpose; on a fielded device a partition change is a reflash rather than an update.

## Toolchain notes

- **C++17.** Arduino-ESP32 2.x compiles at `gnu++11` by default, so the env carries `build_unflags = -std=gnu++11` and `-std=gnu++17`. `core/` uses inline `constexpr` variables (the generated catalog counts) and range-for over brace-initialised tables.

- **`setup()`/`loop()` linkage differs from the Teensy's.** Teensyduino declares them `extern "C"`; Arduino-ESP32 declares them as plain C++. `src/app/entry_point.hpp` branches on `ARDUINO_ARCH_ESP32` for exactly this reason. Getting it backwards fails only in the firmware link and reads like a missing file — it is worth knowing before adding a fourth board.

- **The log ring is sized here, not by `core::Logger`.** Its 13 MiB default does not consult the backend; `src/main_gauge.cpp` passes 4 MiB (32 × 128 KiB) instead. With three signals rather than six that still buys more recording than the CAN product gets.

## Flashing

```sh
pio run -e esp32s3_gauge -t upload
pio device monitor -e esp32s3_gauge
```
