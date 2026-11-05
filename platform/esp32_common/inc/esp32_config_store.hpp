/**
 * @file esp32_config_store.hpp
 * @brief IConfigStore over the ESP32's EEPROM emulation (NVS-backed).
 *
 * The Teensy has real EEPROM and its driver is a thin pass-through. The
 * ESP32 does not, so the Arduino core emulates it over NVS: the whole
 * region is mirrored in RAM, reads are memory reads, and writes are
 * staged until `commit()` pushes the region back to flash.
 *
 * ## Every write commits
 *
 * `write()` commits immediately rather than batching, because
 * `IConfigStore` has no flush in its interface and callers are entitled
 * to assume a write is durable when it returns. core::ConfigStore's
 * partial writers (`save_priority`, `save_signal`) exist precisely so
 * that changing one field is one small write rather than a rewrite of
 * everything -- but NVS rewrites the whole region on any commit, so on
 * this MCU they save bus traffic and not flash wear.
 *
 * That is fine at the rate config actually changes (a handful of writes
 * per configuration session, against a flash rated for 100k cycles). It
 * would not be fine if something started writing config from a loop, and
 * this is the comment that says so.
 *
 * ## Sizing
 *
 * core::Config is under 2KB, so 4KB is the region with generous
 * headroom -- the same size the sibling gauge board reserves, so a config
 * blob is portable between the two.
 */
#pragma once

#include "platform.hpp"

class Esp32ConfigStore : public IConfigStore
{
    public:
        static constexpr size_t CAPACITY_BYTES = 4096;

        static Esp32ConfigStore &instance();

        /** Opens the NVS-backed region. Called lazily by read()/write(),
         * so nothing above platform/ has to know this board needs it.
         * Reads and writes fail safely rather than crashing if it has not
         * succeeded. */
        bool begin();

        bool   read(uint32_t offset, void *buf, size_t len) override;
        bool   write(uint32_t offset, const void *buf, size_t len) override;
        size_t capacity() const override;

    private:
        Esp32ConfigStore() = default;

        bool began_ = false;
};
