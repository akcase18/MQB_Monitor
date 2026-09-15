/**
 * @file esp32s3_config_store.hpp
 * @brief IConfigStore over the ESP32's EEPROM emulation (NVS-backed).
 *
 * The Teensy has real EEPROM and its driver is a thin pass-through. The
 * S3 does not, so the Arduino core emulates it over NVS: the whole region
 * is mirrored in RAM, reads are memory reads, and writes are staged until
 * `commit()` pushes the region back to flash.
 *
 * ## Every write commits
 *
 * `write()` commits immediately rather than batching, because
 * `IConfigStore` has no flush in its interface and callers are entitled
 * to assume a write is durable when it returns. core::ConfigStore's
 * partial writers (`save_priority`, `save_signal`,
 * `DeviceConfigStore::save_sensor`) exist precisely so that changing one
 * field is one small write rather than a rewrite of everything -- but NVS
 * rewrites the whole region on any commit, so on this board they save
 * bus traffic and not flash wear.
 *
 * That is fine at the rate config actually changes (a handful of writes
 * per configuration session, against a flash rated for 100k cycles). It
 * would not be fine if something started writing config from a loop, and
 * this is the comment that says so.
 *
 * ## Sizing
 *
 * core::DEVICE_CONFIG_OFFSET (1024) plus sizeof(core::DeviceConfig)
 * (~1.8KB) is under 3KB, so 4KB is the region with headroom. If it ever
 * stops being enough, `DeviceConfigStore::storage_too_small()` reports it
 * on screen at boot rather than letting every save silently fail.
 */
#pragma once

#include "platform.hpp"

class Esp32s3ConfigStore : public IConfigStore
{
    public:
        static constexpr size_t CAPACITY_BYTES = 4096;

        static Esp32s3ConfigStore &instance();

        /** Opens the NVS-backed region. Call once before any read/write;
         * src/main_gauge.cpp does it as part of bringing the platform up.
         * Reads and writes fail safely rather than crashing if it has not
         * been called. */
        bool begin();

        bool   read(uint32_t offset, void *buf, size_t len) override;
        bool   write(uint32_t offset, const void *buf, size_t len) override;
        size_t capacity() const override;

    private:
        Esp32s3ConfigStore() = default;

        bool began_ = false;
};
