# platform/dash_s3/

The dash product's board layer on an ESP32-S3: the same 480×320 ILI9488 panel, FT6236 touch controller and CAN link the Teensy MicroMod drives on the v3 PCB, re-driven from one chip — and, since the S3 has its own Wi-Fi, the ESP32-C3 relay's job too, so it is one board rather than two. Built by `[env:dash_s3]`, which pairs it with the same `src/main.cpp`, `core/`, `gui/` and `remote/` sources as `env:teensymm`. **Nothing above `platform/` knows this board by name**: the one accommodation, `src/main.cpp` asking `IDisplay::get_frame_buffers()` for whole-screen buffers and rendering DIRECT when it gets them, is a generic hook that only this board's display answers.

The MCU-level half (clock, NVS config store, LittleFS storage) lives in **[esp32_common/](../esp32_common/README.md)**, so a second S3 board only writes what it wires differently. This directory is the board-level half: display, touch, CAN, the in-process wireless relay, the pin map and the partition table.

**[esp32/](../esp32/README.md)** is the ESP32-**C3** Wi-Fi relay the *Teensy* talks to over a UART. This board has no C3: it compiles that directory's relay (`esp32::RelayApp` — everything but the C3's `main.cpp`) into its own firmware and runs it on a second task, with an in-memory pipe where the UART was. `remote/server` on the dash side and the relay on the other are unchanged and still speak `remote/protocol` to each other; they just share a chip.

## Hardware status

Today's target is an ESP32-S3-DevKitC-1 (N16R8) on a bench, with the panel, touch controller and a CAN transceiver on jumper wires. The panel renders (8-bit i80 at 40 MHz on jumper wires; the whole-frame, TE-synced push path is the response to the wipe and tear lines the banded path showed on screen-wide changes). Touch, CAN and the in-process Wi-Fi relay are less proven. The ways each is expected to go wrong, and what that looks like:

| Symptom | Suspect | Where |
|---|---|---|
| Nothing on the panel at all | RD not tied to 3V3; IM jumpers not set for 8-bit 8080; CS/DC swapped | `dash_s3_pins.hpp`, the panel module |
| Torn or corrupt pixels, no error | Bus too fast for the wiring | `LCD_BUS_HZ` in `dash_s3_display.cpp` — start at 10 MHz |
| Believable but wrong colours | RGB565 byte order | `swap_color_bytes` in `dash_s3_display.cpp` |
| Red and blue swapped | `MADCTL_BGR` | same |
| Mirrored or rotated image | MADCTL rotation bits | same — and the touch transform is coupled to them |
| Touch lands in the wrong place | The MADCTL/touch coupling above | `dash_s3_touch.cpp`'s `read()` |
| "TWAI: bus-off" on every loop | No transceiver, or no other node to ACK | `dash_s3_can.cpp`, the bench |
| Screen-wide changes wipe or tear; "no TE edge" once on the console | TE not wired, or on the wrong pin | `DASH_S3_PIN_LCD_TE`, JP1 pin 22 |
| Every pass stalls ~50 ms | Same -- the push task is timing out waiting for TE | same |

## The pinout is not frozen

Every pin is a `-D` with a default, in **`inc/dash_s3_pins.hpp`**. Override them from `platformio.ini` after wiring; do not edit a driver. The defaults avoid the S3's traps — strapping pins (0, 3, 45, 46), native USB (19, 20), the flash bus (26–32), the octal PSRAM bus (33–37, which is also why the panel gets 8 data lines rather than 16), UART0 (43, 44) and the dev kit's LED (38/48).

| | GPIO | |
|---|---|---|
| LCD D0–D7 | 4, 5, 6, 7, 15, 16, 17, 18 | Eight consecutive header positions. RD tied to 3V3. |
| LCD WR / DC / CS / RST | 8 / 9 / 10 / 11 | WR is the i80 peripheral's PCLK. |
| LCD BL | 12 | LEDC PWM at 20 kHz. |
| LCD TE | 47 | Panel's tearing-effect pulse, an input. `-1` to run unsynchronised. |
| Touch SDA / SCL / RST | 13 / 14 / 21 | FT6236 at 0x38, INT unused. |
| CAN TX / RX | 41 / 42 | To an external 3.3 V transceiver. |

No companion-link UART; 39 and 40 are free.

## Stress test without a car

`pio run -e dash_s3_stress -t upload` builds the same firmware with `DASH_S3_FAKE_CAN=1`, which swaps the TWAI driver for `platform/synthetic_can_bus.hpp` — the simulator's synthetic bus. Every readout sweeps continuously at the real 20 Hz poll rate, warnings trip as values cross thresholds, and logging records the changes, all with nothing but the panel wired. No transceiver needed; "TWAI" never starts.

## Bench wiring: ER-TFTM035-6 breakout (JP1) → DevKitC-1

The panel's bare-panel datasheet (`docs/ER-TFT035-6_Datasheet.pdf`) confirms 8-bit 8080 mode (IM = 011) uses **DB0–DB7 only**; DB8–DB17 stay open.

| JP1 | Signal | GPIO | |
|---|---|---|---|
| 1, 40 | VSS | GND | |
| 2 | VDD | 5V *or* 3V3 | Jumper **JP open** = 5 V in (module regulates); **JP shorted** = 3.3 V in. Match the jumper. ~130 mA. |
| 3–10 | DB0–DB7 | 4, 5, 6, 7, 15, 16, 17, 18 | in that order |
| 11–20 | DB8–DB17 | — | open |
| 21 | /RESET | 11 | on-board RC reset is only a pull-up; driving it is fine |
| 23 | LCD_/CS | 10 | |
| 24 | /WR | 8 | |
| 25 | D/C | 9 | |
| 26 | /RD | 3V3 | "fix to IOVCC when not in use" |
| 29 | BL_ON/OFF | 12 | logic input to the on-board transistor: PWM works, dimming is chopped |
| 22 | TE | 47 | rising edge = start of vertical blank; frame pushes start on it |
| 27, 28, 35–38 | SDI/SDO, SD/font/flash CS | — | open |

Pins 30–34 and 39 depend on which touch panel the breakout carries. `DASH_S3_TOUCH_USE_RESISTIVE` in `platformio.ini` must match:

| JP1 | Capacitive (FT6236, `=0`) | GPIO | Resistive (XPT2046, `=1`) | GPIO |
|---|---|---|---|---|
| 30 | CTP_SCL | 14 | RTP_/CS | 2 |
| 31 | CTP_SDA | 13 | /RTP_PEN | — |
| 32 | — | — | SDO (→ MISO) | 1 |
| 33 | — | — | SCL (→ SCK) | 14 |
| 34 | — | — | SDI (→ MOSI) | 13 |
| 39 | CTP_INT | — | — | — |

Jumpers: **8-bit 8080** = J1, J2, J6 shorted, J3, J4, J5 open (plus R1–R10, R20 fitted as 0 Ω — a board ordered for another interface differs in resistors, not just jumpers). **Capacitive touch** = J7, J9, J10, J11 shorted, J8 open; **resistive** = J7, J9, J10, J11 open.

JP1 has no CTP reset pin — the breakout handles it on-board, so `DASH_S3_PIN_TOUCH_RST` toggles nothing. Logic is 3.3 V on both sides.

## Files

- **`inc/dash_s3_pins.hpp`** — every pin, overridable. Start here.
- **`partitions_dash.csv`** — 16 MB, no OTA: a 2 MB factory app and a 13.94 MB LittleFS partition, the same ~1 MB of headroom over `core::Logger`'s 13 MB ring that the Teensy has. Moving to two 2 MB OTA slots is planned — see **[OTA_PLAN.md](OTA_PLAN.md)**.
- **`dash_s3_display.*`** — `DashS3Display`, an ILI9488 driver written directly against ESP-IDF's `esp_lcd` i80 panel-IO layer rather than a display library. The init sequence, MADCTL rotation values and byte order are ported from `lib/ILI948x_t4_mm`, so what shows up should match the Teensy pixel for pixel. Two contracts carry over from the Teensy driver: a push blocks only on the *previous* DMA transfer, then issues and returns (which is what lets `src/main.cpp` call `lv_display_flush_ready` synchronously), and the backlight comes up **off** and stays off until `main.cpp` has seen a real frame land.

  Unlike the Teensy it renders **whole-frame, not in bands**: `get_frame_buffers()` hands LVGL two full-screen RGB565 buffers in PSRAM (64-byte aligned, which is what the i80 driver needs to DMA out of PSRAM; it write-backs the cache itself) and `main.cpp` runs `LV_DISPLAY_RENDER_MODE_DIRECT` against them. `flush()` then only records which rows a pass touched; `flush_end()` hands that row band to a small push task, which waits for the panel's next **TE** rising edge and issues one DMA. Render time is never visible and the write starts at the top of the blanking interval ahead of the scan, which is what removed the wipe and tear lines that band-by-band pushing showed on screen-wide changes. `-D DASH_S3_FRAME_BUFFERS=0` restores the banded path for comparison; with TE unwired (`DASH_S3_PIN_LCD_TE=-1`, or just no wire) the task pushes after a 50 ms timeout and says so once on the console.
- **`dash_s3_res_touch.*`** — `DashS3ResTouch`, `teensy_touch.cpp` (XPT2046 over SPI) on the S3's HSPI host with explicit pins. Active when `DASH_S3_TOUCH_USE_RESISTIVE=1`, which is what `platformio.ini` sets for the bench breakout.
- **`dash_s3_touch.*`** — `DashS3Touch`, `teensy_cap_touch.cpp` with the bus pins made explicit, a 5 ms I2C timeout (the core's 50 ms default would stall the UI on every 10 ms poll if the controller went missing) and a presence probe at boot so a bench without a touch panel doesn't pay for that at all.
- **`dash_s3_can.*`** — `DashS3CanBus` over the S3's TWAI controller. Hardware-filters to the response ID (as the Teensy's mailboxes do), non-blocking transmit/receive, and heals a bus-off from `receive()` so a fixed splice doesn't need a reboot. **Needs an external 3.3 V transceiver** (SN65HVD230, TJA1051T/3).
- **`dash_s3_wireless.*`** — `DashS3Wireless`, the `ISerialLink` `main.cpp` hands to `remote::server`, except there is no serial: `begin()` creates two FreeRTOS stream buffers and a task on core 0 running `esp32::RelayApp` (the C3's SoftAP + config-form relay, from `platform/esp32/`) against a `Stream` over the other ends. `remote::server` writes `CMD_AP_ENABLE` into the pipe and gets `EVT_AP_READY` back; a phone's `GET /` becomes a `REQ_GET_STATE` the relay task blocks on while loopTask answers it — the same two-ended arrangement the Teensy and C3 have, minus the UART. Settings → Wi-Fi on the panel, the QR code, `http://192.168.4.1/` and the form all behave as on the Teensy dash.
- **`dash_s3_platform.cpp`** — `platform_get()`, composing the above with `esp32_common/`.
