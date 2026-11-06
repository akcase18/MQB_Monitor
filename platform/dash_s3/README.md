# platform/dash_s3/

The dash product's board layer on an ESP32-S3: the same 480×320 ILI9488 panel, FT6236 touch controller and CAN link the Teensy MicroMod drives on the v3 PCB, re-driven from one chip — and, since the S3 has its own Wi-Fi, the ESP32-C3 relay's job too, so it is one board rather than two. Built by `[env:dash_s3]`, which pairs it with the same `src/main.cpp`, `core/`, `gui/` and `remote/` sources as `env:teensymm`. **Nothing above `platform/` knows this board by name**: the one accommodation, `src/main.cpp` asking `IDisplay::get_frame_buffers()` for whole-screen buffers and rendering DIRECT when it gets them, is a generic hook that only this board's display answers.

The MCU-level half (clock, NVS config store, LittleFS storage) lives in **[esp32_common/](../esp32_common/README.md)**, so a second S3 board only writes what it wires differently. This directory is the board-level half: display, touch, CAN, the in-process wireless relay and its firmware updater, the pin map and the partition table.

**[esp32/](../esp32/README.md)** is the ESP32-**C3** Wi-Fi relay the *Teensy* talks to over a UART. This board has no C3: it compiles that directory's relay (`esp32::RelayApp` — everything but the C3's `main.cpp`) into its own firmware and runs it on a second task, with an in-memory pipe where the UART was. `remote/server` on the dash side and the relay on the other are unchanged and still speak `remote/protocol` to each other; they just share a chip.

## Hardware status

`env:dash_s3` targets the dash PCB: an ESP32-S3-WROOM-1 **N16R2** (2 MB *quad* PSRAM, hence `memory_type = qio_qspi`), a PAM2804 backlight driver, an FT6236 and no TE line. `env:dash_s3_devkit` is the earlier bench setup, an ESP32-S3-DevKitC-1 (N16R8, octal PSRAM) with the panel, the resistive touch breakout and a CAN transceiver on jumper wires. On the bench the panel renders whole-frame and TE-synced (8-bit i80 at 20 MHz on jumper wires), which is what removed the wipe and tear lines the banded path showed on screen-wide changes. The PCB has no TE, so it renders banded from internal SRAM at 20 MHz instead (see `dash_s3_display.*` below). The console, USB log export and SLCAN sniffer use the native USB port (`ARDUINO_USB_CDC_ON_BOOT=1`), since the PCB doesn't route UART0. Touch, CAN and the in-process Wi-Fi relay are less proven. The ways each is expected to go wrong, and what that looks like:

| Symptom | Suspect | Where |
|---|---|---|
| White screen, backlight on | PSRAM init failed (`opi psram: PSRAM ID read error` on the console: wrong `memory_type` for the module), or the LCD pins don't match the board | `platformio.ini`, `dash_s3_pins.hpp` |
| Nothing on the panel at all | RD not tied to 3V3; IM jumpers not set for 8-bit 8080; CS/DC swapped | `dash_s3_pins.hpp`, the panel module |
| Torn or corrupt pixels, no error | Bus too fast for the wiring | `LCD_BUS_HZ` in `dash_s3_display.cpp` — start at 10 MHz |
| Parts of the screen keep stale content until touched or redrawn | Whole-frame mode on quad PSRAM: LCD DMA starved of bandwidth (15 MHz and up failed on the N16R2) | `DASH_S3_LCD_BUS_HZ` — whole-frame defaults to 10 MHz |
| Believable but wrong colors | RGB565 byte order | `swap_color_bytes` in `dash_s3_display.cpp` |
| Red and blue swapped | `MADCTL_BGR` | same |
| Mirrored or rotated image | MADCTL rotation bits | same — and the touch transform is coupled to them |
| Touch lands in the wrong place | The MADCTL/touch coupling above | `dash_s3_touch.cpp`'s `read()` |
| "TWAI: bus-off" on every loop | No transceiver, or no other node to ACK | `dash_s3_can.cpp`, the bench |
| Screen-wide changes wipe or tear; "no TE edge" once on the console | TE not wired, or on the wrong pin | `DASH_S3_PIN_LCD_TE`, JP1 pin 22 |
| Every pass stalls ~50 ms | Same -- the push task is timing out waiting for TE | same |

## Pinout

Every pin is a `-D` with a default, in **`inc/dash_s3_pins.hpp`**. The defaults are the PCB's; `env:dash_s3_devkit` overrides them with the dev kit wiring. Override from `platformio.ini`; do not edit a driver.

| | PCB | DevKitC-1 | |
|---|---|---|---|
| LCD D0–D7 | 42, 41, 40, 39, 38, 37, 36, 35 | 4, 5, 6, 7, 15, 16, 17, 18 | RD tied to 3V3. 35–37 are only free because the R2's PSRAM is quad. |
| LCD WR / DC / CS / RST | 21 / 14 / 13 / 8 | 8 / 9 / 10 / 11 | WR is the i80 peripheral's PCLK. |
| LCD BL | 10 | 12 | LEDC PWM at 500 Hz, the PAM2804's EN-pin dimming frequency. |
| LCD TE | — (`-1`) | 47 | Unsynchronised push on the PCB. |
| Touch SDA / SCL / RST | 1 / 2 / 11 | 13 / 14 / 21 | FT6236 at 0x38, INT (IO12 on the PCB) unused. |
| CAN TX / RX | 47 / 48 | 41 / 42 | To an external 3.3 V transceiver. |

## Firmware updates

Over Wi-Fi from a phone, with the panel confirming before anything is installed. The design and its reasoning are in **[OTA_PLAN.md](OTA_PLAN.md)**.

1. **Once, over USB:** `pio run -e dash_s3 -t upload`. This writes the A/B partition table. Existing logs are lost this once; settings live in NVS and survive.
2. Build the update. For anything you hand out, tag the commit and run `python3 tools/release.py` (see [Releases](#releases)). For your own bench, `pio run -e dash_s3` produces `.pio/build/dash_s3/firmware.bin`, versioned by `git describe` at build time (`tools/fw_version.py`).
3. On the panel, open Settings → Wi-Fi. Join the AP and open `http://192.168.4.1/update`, or use the **Update** link at the bottom of the config page.
4. Opening the page makes the panel ask **Update firmware?** (30 s countdown). Tap **Start** and the panel goes into update mode; the page's file picker unlocks only then, and uploads are refused without that tap. Update mode gives up after 5 minutes with nothing sent.
5. Choose the firmware file (the page shows its version, and marks a bench stress build) and tap **Send to dash**.
6. The panel shows progress, then asks **Install and restart?**. Nothing changes until Install is tapped there. A failed upload leaves the panel waiting for another try; the X while waiting (or Cancel on the Install popup) leaves update mode.
7. The new firmware gets 10 s to prove itself: frames reaching the panel, `loop()` coming round under a 5 s hang watchdog, and the Wi-Fi relay that serves the update page running. A crash, a hang, or a power cut before then boots the previous firmware, which says **Update failed — Still on *version***. So does a relay that never came up, at the next restart. Once proven, there's no going back from the panel: send the older file the same way, or flash it over USB.

Refused before anything is written:

- a file built for the C3, for another project, or for the other dash hardware (`DASH_S3_HW_VARIANT`: `pcb` vs `devkit`);
- a bench stress build (`env:dash_s3_stress`), which marks its variant `pcb-stress` so it can't be mistaken for the real firmware — and a dash running one refuses real firmware over the air too;
- firmware built for a different partition table. OTA only writes an app slot, so that kind of update has to go over USB; the page says so.

USB always works as a fallback, since the S3's download mode is in ROM.

## Releases

`tools/release.py` builds what gets handed out, for the release hardware only: this PCB (`env:dash_s3`) and the Teensy dash (`env:teensymm` and its C3 relay, `env:esp32c3`). The dev kit and stress builds are never released. Tag a clean commit (`git tag v1.2.0`), run it, and upload `releases/v1.2.0/` to the GitHub release for that tag. It holds:

- `dash_s3/mqb-dash-s3-<version>.bin` — the file to send from the phone;
- `bootloader.bin`, `partitions.bin`, `boot_app0.bin` and `flash-usb.sh` — the same release over USB (`./flash-usb.sh /dev/cu.usbmodem…`, needs `pip install esptool`). It writes each file at its own offset, so the dash keeps its settings; one merged image would overwrite NVS;
- `teensymm/mqb-teensymm-<version>.hex` — the Teensy build, installed over USB with PJRC's Teensy Loader;
- `esp32c3/` — the Teensy dash's C3 relay, laid out like the USB files above (`flash-usb.sh` included). Flash it alongside the Teensy from the same release, since the two share no protocol version;
- `manifest.json` and `RELEASE.md` — sizes and SHA-256s, the partition layout and settings version, and, against the previous tag, whether this release needs USB (partition table changed), changes the stored settings layout, or ships a new bootloader (which only a USB install applies).

`--dev` makes a trial release from an untagged or uncommitted tree, under `releases/dev-…`. `releases/` is git-ignored.

## Stress test without a car

`pio run -e dash_s3_stress -t upload` builds the same firmware with `DASH_S3_FAKE_CAN=1`, which swaps the TWAI driver for `platform/synthetic_can_bus.hpp` — the simulator's synthetic bus. Every readout sweeps continuously at the real 20 Hz poll rate, warnings trip as values cross thresholds, and logging records the changes, all with nothing but the panel wired. No transceiver needed; "TWAI" never starts.

## Bench wiring (`env:dash_s3_devkit`): ER-TFTM035-6 breakout (JP1) → DevKitC-1

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

Pins 30–34 and 39 depend on which touch panel the breakout carries. `DASH_S3_TOUCH_USE_RESISTIVE` in `env:dash_s3_devkit` must match:

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
- **`partitions_dash.csv`** — 16 MB: two 2 MB OTA app slots and an 11.94 MB LittleFS partition, the same ~1 MB of headroom over `core::Logger`'s ring that the Teensy has. The ring is 11 MB here (`LOG_CHUNK_COUNT=44` in `platformio.ini`) rather than the Teensy's 13 MB, about 2.5 hours of worst-case retention instead of 3.
- **`dash_s3_firmware.*`** — which firmware this is, and the board side of rollback. Every image carries a `DashS3FirmwareId` (magic, build variant, version, partition-layout CRC) in `.rodata_custom_desc`, at byte 288 of the `.bin`, which is how an upload is identified from its first chunk. Images from before the layout field carry no tag and count as the original A/B table (`LEGACY_PARTITION_LAYOUT`). It also overrides the Arduino core's `verifyRollbackLater()` so a new image boots on probation, and checks at compile time that the log ring fits the LittleFS partition. It implements `platform.hpp`'s FIRMWARE hooks: `platform_update_path_ready()` (the relay task is running, `DashS3Wireless::relay_running()`), `platform_firmware_healthy()` ends the probation, `platform_take_failed_update()` reports a rollback once (from an NVS note written at install), and `platform_hang_watchdog()` is the Arduino loop watchdog.
- **`dash_s3_ota.*`** — `DashS3Updater`, the relay-side updater, plugged into `esp32::RelayApp` as a `RelayExtension`. It serves `/update` and `/api/version`, streams a raw `POST /update` body into the inactive slot with `esp_ota_*`, and reports to the panel over `EVT_OTA_*`. Only `CMD_OTA_COMMIT` from the panel switches slots. Arduino's `Update` library isn't used because its `end()` switches slots itself.
- **`web/update.html`** — the upload page, gzipped in by `tools/web/embed_web.py`.
- **`dash_s3_display.*`** — `DashS3Display`, an ILI9488 driver written directly against ESP-IDF's `esp_lcd` i80 panel-IO layer rather than a display library. The init sequence, MADCTL rotation values and byte order are ported from `lib/ILI948x_t4_mm`, so what shows up should match the Teensy pixel for pixel. Two contracts carry over from the Teensy driver: a push blocks only on the *previous* DMA transfer, then issues and returns (which is what lets `src/main.cpp` call `lv_display_flush_ready` synchronously), and the backlight comes up **off** and stays off until `main.cpp` has seen a real frame land.

  Unlike the Teensy it renders **whole-frame, not in bands**: `get_frame_buffers()` hands LVGL two full-screen RGB565 buffers in PSRAM (64-byte aligned, which is what the i80 driver needs to DMA out of PSRAM; it write-backs the cache itself) and `main.cpp` runs `LV_DISPLAY_RENDER_MODE_DIRECT` against them. `flush()` then only records which rows a pass touched; `flush_end()` hands that row band to a small push task, which waits for the panel's next **TE** rising edge and issues one DMA. Render time is never visible and the write starts at the top of the blanking interval ahead of the scan, which is what removed the wipe and tear lines that band-by-band pushing showed on screen-wide changes. **Whole-frame is on only when `DASH_S3_PIN_LCD_TE` is set** (the default follows it; `-D DASH_S3_FRAME_BUFFERS=0/1` forces it). Without TE the push lands mid-scan and tears anyway, and on the PCB's quad PSRAM whole-frame was the slower path: a full redraw took ~57 ms, against ~33–46 ms banded from internal SRAM with fifth-of-a-screen bands (`DRAW_BUF_DIVISOR=5`). With TE configured but unwired (`DASH_S3_PIN_LCD_TE=-1`, or just no wire) the task pushes after a 50 ms timeout and says so once on the console.
- **`dash_s3_res_touch.*`** — `DashS3ResTouch`, `teensy_touch.cpp` (XPT2046 over SPI) on the S3's HSPI host with explicit pins. Active when `DASH_S3_TOUCH_USE_RESISTIVE=1`, which is what `env:dash_s3_devkit` sets for the bench breakout.
- **`dash_s3_touch.*`** — `DashS3Touch`, `teensy_cap_touch.cpp` with the bus pins made explicit, a 5 ms I2C timeout (the core's 50 ms default would stall the UI on every 10 ms poll if the controller went missing) and a presence probe at boot so a bench without a touch panel doesn't pay for that at all.
- **`dash_s3_can.*`** — `DashS3CanBus` over the S3's TWAI controller. Hardware-filters to the modules' response IDs (one code/mask pair, so everything sharing the range's top bits, e.g. 0x700–0x7FF), non-blocking transmit/receive, and heals a bus-off from `receive()` so a fixed splice doesn't need a reboot. **Needs an external 3.3 V transceiver** (SN65HVD230, TJA1051T/3).
- **`dash_s3_wireless.*`** — `DashS3Wireless`, the `ISerialLink` `main.cpp` hands to `remote::server`, except there is no serial: `begin()` creates two FreeRTOS stream buffers and a task on core 0 running `esp32::RelayApp` (the C3's SoftAP + config-form relay, from `platform/esp32/`) against a `Stream` over the other ends. `remote::server` writes `CMD_AP_ENABLE` into the pipe and gets `EVT_AP_READY` back; a phone's `GET /` becomes a `REQ_GET_STATE` the relay task blocks on while loopTask answers it — the same two-ended arrangement the Teensy and C3 have, minus the UART. Settings → Wi-Fi on the panel, the QR code, `http://192.168.4.1/` and the form all behave as on the Teensy dash.
- **`dash_s3_platform.cpp`** — `platform_get()`, composing the above with `esp32_common/`.
