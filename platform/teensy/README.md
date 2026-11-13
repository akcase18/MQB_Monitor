# platform/teensy/

The real-hardware implementation of every [platform](../README.md) interface, for the Teensy MicroMod board this project runs on in the car.

## Files

- **`inc/teensy_can.hpp`** / **`src/teensy_can.cpp`** — `TeensyCanBus`, wrapping `lib/FlexCAN_T4` (mailbox filter, baud, four mailboxes filtering the modules' response-ID range). Its sniffer mode switches CAN3 to an accept-all FIFO whose interrupt copies each frame into a 512-entry (8 KB) ring in **RAM2** (`DMAMEM`) — not into FlexCAN_T4's own RX ring, which stays 16 deep because it lives in RAM1 whether sniffing or not. The 6-deep hardware FIFO alone lasts ~1.5 ms on a busy bus, so frames have to leave it from the ISR. `sniff_end()` re-runs `begin()` with the boot-time arguments. Bench mode is the same accept-all FIFO, but its interrupt hands each frame, stamped with `micros()`, straight to the bench engine, and PIT channel 0 (programmed directly; `IntervalTimer`'s code would land in ITCM) ticks it every 50 µs at the same NVIC priority. Transmits use FlexCAN_T4's sequential path, one mailbox plus an in-order queue drained by that mailbox's interrupt, so ISO-TP consecutive frames can't overtake each other. The mailbox interrupt masks are written directly, because `enableMBInterrupts()` hits the `.flashmem` section conflict described above.
- **`inc/teensy_display.hpp`** / **`src/teensy_display.cpp`** — `TeensyDisplay`, wrapping `lib/ILI948x_t4_mm` for the 480×320 ILI948x-family panel (init, pixel flush, backlight PWM).
- **`inc/teensy_cap_touch.hpp`** / **`src/teensy_cap_touch.cpp`** — `TeensyCapTouch`, a minimal I2C polling driver for the FT6236 capacitive touch controller (currently on the PCB). Reads the standard FocalTech FT6x06-family touch-count/X/Y registers directly, no vendor library.
- **`inc/teensy_touch.hpp`** / **`src/teensy_touch.cpp`** — `TeensyTouch`, a raw SPI XPT2046-style resistive touch controller driver (multi-sample averaging + rotation remap), ported directly from the original firmware's touch driver. Kept for whenever a resistive panel is back in the design.
- **`inc/teensy_clock.hpp`** / **`src/teensy_clock.cpp`** — `TeensyClock`, wrapping Arduino's `millis()`.
- **`inc/teensy_config_store.hpp`** / **`src/teensy_config_store.cpp`** — `TeensyConfigStore`, wrapping Teensy's `EEPROM.h` for persisted config storage.
- **`inc/teensy_serial_link.hpp`** / **`src/teensy_serial_link.cpp`** — `TeensySerialLink`, a spare hardware `Serial` (Serial4) carrying the `remote/protocol` framed byte stream to/from the companion ESP32-C3.
- **`inc/teensy_storage.hpp`** / **`src/teensy_storage.cpp`** — `TeensyStorage`, backed by `LittleFS_Program` on a 14MB region of the board's internal MCU program flash (currently active — see below; not the external QSPI chip, which hung on real hardware and was abandoned). Sized against [core/logging](../../core/logging/README.md)'s `CHUNK_COUNT * CHUNK_SIZE_BYTES` (13MiB) plus headroom for LittleFS's own block metadata and firmware growth. `core::Config` is *not* in here — `TeensyConfigStore` keeps that in EEPROM; this region is the log ring and nothing else. `teensy_size` reports ~16.02MB free for files after the current firmware, so ~15.3MiB is the hard ceiling on this constant.
- **`inc/teensy_sd_card.hpp`** / **`src/teensy_sd_card.cpp`** — `TeensySdCard`, backed by SdFat over SPI1 for a Micro-SD slot. No slot exists on the current PCB; kept for whenever one is added back.
- **`src/teensy_platform.cpp`** — `platform_get()` for this board: builds the `Platform` struct from the singletons above.

Each class is a lazily-constructed Meyer's singleton (`instance()`), matching the pattern `src/main.cpp`'s composition root itself uses.

### Two interfaces, two switchable implementations

`ITouch` and `IStorage` each have two Teensy-side implementations, both always compiled (so neither one can silently bit-rot from lack of build coverage), with a single `#define` in `teensy_platform.cpp` picking which one actually gets wired into `Platform`:

```cpp
#define TEENSY_TOUCH_USE_RESISTIVE 0   // 0 = TeensyCapTouch (FT6236), 1 = TeensyTouch (XPT2046)
#define TEENSY_STORAGE_USE_SD      0   // 0 = TeensyStorage (onboard flash), 1 = TeensySdCard (Micro-SD)
```

Flipping either is a one-line change — no other file needs to change to switch backends. Each `#define` gates which backend's header is `#include`d in `teensy_platform.cpp` (not just which `::instance()` is called), because the two touch/storage libraries can define conflicting macros of the same name if both headers land in one translation unit (confirmed for `SdFat.h`/`LittleFS.h`, both defining `FILE_READ`/`FILE_WRITE` with different values) — each backend's own `.cpp` still includes only its own header, so this only matters in the composition file.

## Building/flashing

```sh
pio run -e teensymm -t upload
```
