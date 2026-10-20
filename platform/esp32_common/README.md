# platform/esp32_common/

The MCU-level half of every ESP32-S3 board layer: the pieces that depend on the chip and the Arduino-ESP32 core but not on what is soldered around it.

- **`Esp32Clock`** — `IClock` over `millis()`.
- **`Esp32ConfigStore`** — `IConfigStore` over the Arduino EEPROM emulation (NVS-backed), 4 KB. Opens itself on first use. **Every write commits** — `IConfigStore` has no flush, so a caller is entitled to assume durability on return. NVS rewrites the whole region on any commit, so this is fine at the rate config actually changes and not fine if something ever writes config from a loop.
- **`Esp32Storage`** — `IStorage` over LittleFS on the internal flash, mounting the partition labelled `spiffs` (the label `LittleFS.begin()` looks for; the filesystem inside is LittleFS). Formats on a failed mount, so the first boot after flashing creates the filesystem instead of leaving logging broken until somebody formats by hand — expect that first boot to take a few seconds on a 14 MB partition.

A board directory (`platform/dash_s3/` today, a gauge board later) supplies the board-level half — display, touch/buttons, CAN/analog, the UART to the wireless companion, its pin map and its partition table — and composes all of it into `platform_get()`. The split is by *what changes between boards*, not by peripheral: a second S3 board gets these three for free and writes only what it actually wires differently.

Nothing here has a pin number in it.
