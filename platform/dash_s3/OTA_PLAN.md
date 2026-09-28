# OTA firmware updates — plan (not implemented)

How the ESP32-S3 dash will take firmware updates over Wi-Fi. Nothing here is built yet; this is the agreed design so implementation can start from it. The Teensy + C3 pair is not getting OTA — it is being retired in favour of this board, and self-flashing a Teensy through the C3 (FlasherX-style staging and copy) was judged not worth building for hardware that is on its way out.

The S3 makes this much simpler than it would be on the Teensy: ESP-IDF has native A/B app slots and bootloader rollback, so the running image is never overwritten and a bad update reverts itself.

## Starting point

- **Partitions:** [`partitions_dash.csv`](partitions_dash.csv) is deliberately no-OTA today — one 2 MB `factory` app and a 13.94 MB LittleFS log partition.
- **App size:** 1.22 MB (`env:dash_s3`, 58% of the current 2 MB slot), measured Sept 2026.
- **Rollback support is already in the toolchain:** the Arduino-ESP32 SDK in use (`framework-arduinoespressif32` 3.20017) ships its bootloader with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, and the core exposes a weak `verifyRollbackLater()` hook (`cores/esp32/esp32-hal-misc.c`). Left at its default (`false`), the core marks every new image valid at startup. Overriding it to return `true` puts a new image on probation until the firmware calls `esp_ota_mark_app_valid_cancel_rollback()` itself.
- **Wi-Fi/HTTP is already there:** `DashS3Wireless` runs the C3's relay (`esp32::RelayApp`) in-process on core 0, serving the config form on the SoftAP at `http://192.168.4.1/`. The update page plugs into that same `WebServer`.

## Design

### 1. Partition table — two 2.5 MB app slots

```
# Name,    Type, SubType, Offset,   Size
nvs,       data, nvs,     0x9000,   0x5000
otadata,   data, ota,     0xE000,   0x2000
app0,      app,  ota_0,   0x10000,  0x280000
app1,      app,  ota_1,   0x290000, 0x280000
spiffs,    data, spiffs,  0x510000, 0xAF0000
```

- **Why 2.5 MB and not 3 MB:** 2.5 MB is about 2× today's 1.22 MB app. The largest additions we might plausibly make still fit: HTTPS downloads (`esp_https_ota`, the TLS cert bundle) and BLE (NimBLE) would each add a few hundred KB at most. Each extra 0.5 MB per slot would cost 1 MB of log space, so 3 MB slots would buy headroom we don't expect to use. The case for 3 MB was that the partition table can only be changed over USB, but that's a small cost on a single dash where USB stays accessible. If the app ever does outgrow 2.5 MB, it's one USB reflash.
- **Log ring shrinks from 13 MiB to 10 MiB:** the ring becomes 40 × 256 KiB chunks instead of 52. That keeps the same ~0.94 MiB of LittleFS headroom the current table has (10.94 MiB partition), and worst-case retention drops from about 3 hours to about 2.3. `src/main.cpp` currently builds `core::Logger` with the default `CHUNK_COUNT`, so the S3 needs to pass 40 explicitly. The ring doesn't check the partition's capacity (see TODO.md), so this has to be done by hand.
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

1. New `partitions_dash.csv` and a 40-chunk `core::Logger` ring on the S3. This first flash has to go over USB.
2. `/update` upload handler plus the identity/version marker (`platform/esp32`, build flags). Items 1–2 alone make a working laptop-driven update.
3. `EVT_OTA_*` / `CMD_OTA_COMMIT` in `remote/protocol`; the update and confirm screens in `gui/screens`; pausing logging in `src/main.cpp`.
4. `verifyRollbackLater()` override, health-check mark-valid, task watchdog on loopTask, and the rollback notice.
5. Version number on the stored settings.
