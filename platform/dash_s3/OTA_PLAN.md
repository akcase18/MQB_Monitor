# OTA firmware updates — design

How the ESP32-S3 dash takes firmware updates over Wi-Fi. This is the agreed design, and it is built. Where the code had to differ, **As built** at the end says how and why. How to use it is in [README.md](README.md#firmware-updates). The Teensy + C3 pair is not getting OTA — it is being retired in favour of this board, and self-flashing a Teensy through the C3 (FlasherX-style staging and copy) was judged not worth building for hardware that is on its way out.

The S3 makes this much simpler than it would be on the Teensy: ESP-IDF has native A/B app slots and bootloader rollback, so the running image is never overwritten and a bad update reverts itself.

## Starting point

- **Partitions:** [`partitions_dash.csv`](partitions_dash.csv) was deliberately no-OTA — one 2 MB `factory` app and a 13.94 MB LittleFS log partition.
- **App size:** 1.22 MB (`env:dash_s3`, 58% of the current 2 MB slot), measured Sept 2026.
- **Rollback support is already in the toolchain:** the Arduino-ESP32 SDK in use (`framework-arduinoespressif32` 3.20017) ships its bootloader with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, and the core exposes a weak `verifyRollbackLater()` hook (`cores/esp32/esp32-hal-misc.c`). Left at its default (`false`), the core marks every new image valid at startup. Overriding it to return `true` puts a new image on probation until the firmware calls `esp_ota_mark_app_valid_cancel_rollback()` itself.
- **Wi-Fi/HTTP is already there:** `DashS3Wireless` runs the C3's relay (`esp32::RelayApp`) in-process on core 0, serving the config form on the SoftAP at `http://192.168.4.1/`. The update page plugs into that same `WebServer`.

## Design

### 1. Partition table — two 2 MB app slots

```
# Name,    Type, SubType, Offset,   Size
nvs,       data, nvs,     0x9000,   0x5000
otadata,   data, ota,     0xE000,   0x2000
app0,      app,  ota_0,   0x10000,  0x200000
app1,      app,  ota_1,   0x210000, 0x200000
spiffs,    data, spiffs,  0x410000, 0xBF0000
```

- **Why 2 MB:** A/B doesn't need slots twice the app's size. Each slot holds one complete image, so a slot only has to fit the largest firmware we'll ever install. 2 MB is the same size as today's single `factory` slot, so the app keeps the ~0.78 MB of growth room it has now. Every extra MB per slot costs 2 MB of log space, because there are two slots. Larger options that were considered:
  - **2.5 MB** — ~1.28 MB headroom, 10 MiB log ring.
  - **3 MB** — ~1.78 MB headroom, 9 MiB log ring.

  Neither was worth the lost log space. If HTTPS downloads (`esp_https_ota`) plus BLE (NimBLE) ever push the app past 2 MB, resizing is one USB reflash, and USB stays accessible on this single dash.
- **Log ring shrinks from 13 MiB to 11 MiB:** the ring becomes 44 × 256 KiB chunks instead of 52. That keeps the same ~0.94 MiB of LittleFS headroom the current table has (11.94 MiB partition), and worst-case retention drops from about 3 hours to about 2.5. `src/main.cpp` currently builds `core::Logger` with the default `CHUNK_COUNT`, so the S3 needs to pass 44 explicitly. The ring doesn't check the partition's capacity (see TODO.md), so this has to be done by hand.
- **Migration is a one-time USB flash:** it writes the new partition table plus `otadata`. LittleFS moves, so existing logs are lost once. Settings are in NVS at the same offset, so they survive. After this, updates never need USB.

### 2. Upload — `/update` on the existing config site

- Add a file-upload form on its own page, served by `RelayApp`'s `WebServer` next to the config form.
- The upload handler streams chunks straight into the inactive slot with Arduino's `Update` library (`Update.begin(UPDATE_SIZE_UNKNOWN)` → `write()` per chunk → `end()`). Nothing is buffered in RAM, and the running slot is never touched.
- `Update.end()` checks the ESP image's embedded SHA-256, so a truncated or corrupt upload is rejected.
- **Identity check before writing:** embed a board/project marker and a version string in the firmware. Check them against the first chunk and reject the upload *before* `Update.begin()` if they don't match. That stops the C3 relay's `firmware.bin` or another board's build from ever being written.
- The uploaded file is the `firmware.bin` PlatformIO already produces (`.pio/build/dash_s3/firmware.bin`).
- **Timing:** the transfer over the SoftAP takes a few seconds. Writing ~1.2 MB to flash, including sector erases, takes about 10–15 s.

### 3. Keep the display informed — new `remote/protocol` messages

The upload runs in the relay task (core 0), but the display and all app state live in loopTask. These two sides already talk only through `remote/protocol`, so the update does the same. Add:

- `EVT_OTA_BEGIN` (relay → dash) — the dash switches to a plain "Updating…" screen and pauses `core::Logger`. The screen matters for more than looks: flash erase/write suspends the cache on both cores, which also blocks CPU access to the PSRAM framebuffers, so rendering will stutter during the write anyway.
- `EVT_OTA_PROGRESS` (relay → dash) — bytes written / total, for a progress bar.
- `EVT_OTA_RESULT` (relay → dash) — image written and verified OK, or failed with a reason. On failure the dash leaves the update screen and resumes logging.
- `CMD_OTA_COMMIT` (dash → relay) — sent only after the user confirms on the panel (step 4). The relay calls `esp_ota_set_boot_partition()` for the new slot and restarts.

All four are new message types only; no existing message changes.

### 4. Confirm on the panel

After `EVT_OTA_RESULT` reports success, the dash shows "Install v*x.y*? Restart now" with a confirm button. The boot slot changes only on that tap.

This is the security gate. The SoftAP may be open (an empty password is supported), so without the tap anyone who joined while the Wi-Fi screen was up could install firmware. Requiring a tap on the panel means someone has to be in the car, which is enough protection without signed firmware or secure boot.

### 5. First boot — probation and rollback

- Override `verifyRollbackLater()` to return `true` in the S3 platform layer. A freshly installed image then boots as `ESP_OTA_IMG_PENDING_VERIFY`.
- Mark it valid (`esp_ota_mark_app_valid_cancel_rollback()`) only after it has proven itself: display initialised and a frame pushed, touch responding, CAN started, settings loaded, and loopTask running normally for ~10 s.
- If the image crashes or boot-loops before that, the bootloader boots the previous slot on the next reset.
- **Hangs need the watchdog:** rollback is triggered only by a reset, so a hang with no reset would leave the bad image in place indefinitely. loopTask must be subscribed to the task watchdog so a hang causes a reset.
- **Rollback notice:** at startup, check whether the last attempted slot ended up invalid (`esp_ota_get_last_invalid_partition()` / `esp_ota_get_state_partition()`). If so, show "Update failed — still on v*x.y*" once.

### 6. Settings compatibility

Config stays in NVS through updates and rollbacks, so it has to work in both directions:

- New firmware must read settings saved by old firmware (migration).
- After a rollback, old firmware may find settings written by the newer version. Give the stored settings a version number; firmware that finds a version newer than it understands falls back to defaults instead of misreading the data.

## Recovery

The S3's serial/USB download mode is in mask ROM, so `esptool` over USB can always recover the board, whatever state the flash is in. With A/B slots and rollback it should rarely be needed.

## Optional extras

- **Bench convenience:** `ArduinoOTA` in a dev-only env, so `pio run -e dash_s3 -t upload --upload-port 192.168.4.1` works from a laptop joined to the SoftAP. Keep it out of release builds — it would bypass the panel confirmation.
- **Download updates directly:** station mode on your home Wi-Fi plus `esp_https_ota`, pulling a release from a URL. It uses the same slots, probation and rollback, so it can be added later without changing anything above.

## Work items, in order

All done.

1. New `partitions_dash.csv` and a 44-chunk `core::Logger` ring on the S3 (`LOG_CHUNK_COUNT`). This first flash has to go over USB.
2. `/update` upload handler plus the identity/version marker (`dash_s3_ota.*`, `dash_s3_firmware.*`, `tools/fw_version.py`).
3. `EVT_OTA_*` / `CMD_OTA_COMMIT` in `remote/protocol`; `remote::server::RemoteOtaController`; `gui::FirmwareUpdateScreen` with its confirm popup; pausing logging in `src/main.cpp`.
4. `verifyRollbackLater()` override, health-check mark-valid, the loop watchdog, and the rollback notice.
5. Settings compatibility in `core::ConfigStore::load()`.

## As built

- **`esp_ota_*`, not Arduino's `Update`.** `Update.end()` calls `esp_ota_set_boot_partition()` itself, so the new image would boot on the next reset with no tap on the panel. `DashS3Updater` drives `esp_ota_begin()` (with `OTA_WITH_SEQUENTIAL_WRITES`, so sectors are erased as the image reaches them), `esp_ota_write()` and `esp_ota_end()` directly. `esp_ota_end()` checks the checksum and appended SHA-256, as planned. `esp_ota_set_boot_partition()` runs only on `CMD_OTA_COMMIT`, and it re-verifies the image first.
- **The identity marker sits in `.rodata_custom_desc`.** `esp_app_desc_t` can't identify the app: under Arduino its `project_name` is `arduino-lib-builder` for every build. ESP-IDF's linker script places `.rodata_custom_desc` straight after it, so `DashS3FirmwareId` (magic `MQB-DASH-S3`, hardware variant, version) lands at byte 288 of every `.bin`. That is inside the first upload chunk, so the image is identified before `esp_ota_begin()`. The check also covers the image magic, the chip ID (ESP32-S3) and the app-descriptor magic. The variant (`pcb`, or `devkit` from `DASH_S3_HW_VARIANT`) keeps a dev-kit build off the PCB and the reverse. The version is `git describe --tags --always --dirty`, stamped into a generated header.
- **The upload is the raw request body, not a multipart form.** WebServer's raw mode streams the body in 1436-byte chunks and reports `Content-Length` up front, which gives the progress total. The route is a `RequestHandler` subclass rather than `server.on()`, because `server.on()` sends multipart and raw bodies to the same callback and `server.raw()` is null for multipart. WebServer's raw read spins without yielding while it waits for data, for up to the client's 5 s timeout. Left alone, a stalled phone would starve core 0's idle task right at the task watchdog's 5 s limit. Core 0's idle watchdog is therefore lifted for the duration of an upload.
- **Protocol payloads.** `EVT_OTA_BEGIN` carries the total size and the version, and is sent only once the identity check passes, so a wrong file never reaches the panel. `EVT_OTA_PROGRESS` is the bytes written, every 64 KB. `EVT_OTA_RESULT` is one `OtaResult` byte. A commit the relay can't carry out also comes back as an `EVT_OTA_RESULT`. The phone gets a plain-text reason with each refusal.
- **Update mode needs a tap too, before any upload** (added after the first bench test). Previously, an upload from any phone on the AP would take over the panel's screen unannounced. Now the page asks as soon as it opens (`POST /api/update/request` → `EVT_OTA_REQUEST`), keeping its file picker locked until the answer comes, and the panel puts up **Update firmware?** with Cancel/Start and a 30 s countdown, the same shape as the sniffer's request. Start sends `CMD_OTA_ARM`, and the relay refuses uploads and commits until then (`ErrNotArmed`, HTTP 403). The page polls `GET /api/update/status` for the answer rather than holding a request open, because the server serves one request at a time. Update mode then shows **Waiting for firmware** with an X to leave (`CMD_OTA_DISARM`, which also drops any unused verified image). A failed upload returns there rather than ending update mode. The AP going down ends it too, as do 5 minutes with nothing sent (`OTA_WAIT_TIMEOUT_MS`).
- **The confirm step is a popup** (`create_confirm_popup()`, "Install and restart?" over the version) on the update screen. Cancel leaves the image in the spare slot, unused until the next upload overwrites it. While the update is in flight, `src/main.cpp` keeps the AP's idle timeout from firing and puts the update screen back if anything else loads over it.
- **Health check.** The image is marked valid 10 s after boot, provided at least one frame has been flushed. The 10 s itself is proof that `loop()` came round under the watchdog. Touch and CAN aren't checked: touch can't be without a press, and TWAI starts fine with no bus attached. A power cut inside those 10 s therefore also rolls back, and the old firmware says so.
- **Hang watchdog.** This is the Arduino core's loop watchdog (`enableLoopWDT()`): 5 s, and a reset rather than a warning (`CONFIG_ESP_TASK_WDT_PANIC`). It is armed at the end of `setup()`. `LOGDUMP` and log erase turn it off around themselves, since both block on purpose for longer than that. It is exposed as `platform_hang_watchdog()`, a no-op on the other boards.
- **Rollback notice.** At commit, the relay writes the target slot and version to NVS (namespace `ota`). The old firmware, finding itself running instead, reports the note once through `platform_take_failed_update()` and clears it. The new firmware clears it only from the health check. If it cleared the note on first read, a new image that crashed right after reading it would erase the old firmware's only evidence of the rollback.
- **Settings compatibility** uses the config header's existing `CURRENT_VERSION`. A newer version loads defaults and is not written back, so reinstalling the newer firmware finds its settings again unless something saves in between.
- **Internal RAM.** The first build of this broke the config page: the phone's request arrived, but sending the 12 KB page stalled for 10 s and gave up. With the AP up, only ~19 KB of internal RAM was left, and that is what the Wi-Fi stack's transmit buffers come from. The pre-OTA firmware had ~4 KB more, mostly what an always-built update screen took in LVGL objects. The update screen is now created on entering update mode and destroyed on leaving, like the sniffer's. `dash_s3_platform.cpp` also lowers the SDK's internal-malloc cutoff from 4096 to 2048 bytes, so the config store's 4 KB EEPROM mirror and the QR-code canvas land in PSRAM. Rendering throughput was unchanged on the stress build. Idle internal RAM went from 47 KB (pre-OTA) to 58 KB; during a 1.2 MB upload the low point was 17 KB. Verified on the PCB 2026-10-25: request, Start, upload (13 s), Install, reboot into app1, marked valid.
- **One more constraint from ESP-IDF:** `esp_ota_begin()` refuses while the running image is still on probation. A second update has to wait the 10 s.
