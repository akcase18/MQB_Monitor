/**
 * @file simulator_config_store.cpp
 * @brief IConfigStore backed by a local file, standing in for Teensy's
 * EEPROM. Unwritten regions read back as 0xFF to mirror erased-flash
 * behavior, so core/config's magic-number detection works the same on
 * both platforms.
 */

#include "simulator_config_store.hpp"

#include "simulator_config.hpp"

#include <stdio.h>
#include <string.h>

namespace
{
    constexpr size_t CONFIG_STORE_CAPACITY = 4096;

    /**
     * One blob per product, the same way the window position file is.
     * The dev host stands in for two different devices, and they are not
     * interchangeable: both write a core::Config with the same magic and
     * version, but the struct's size and the meaning of every signal id
     * inside it come from that product's own catalog. Sharing one file
     * makes the gauge simulator read the MQB product's panel assignment,
     * which resolves ids that do not exist in its catalog and silently
     * blanks the panels that got them.
     *
     * **This does not make real hardware safe.** A physical device with a
     * stale blob from the other product hits exactly the same problem,
     * and the fix there is a distinct MAGIC_NUMBER per product so the
     * header is rejected rather than reinterpreted -- see the build
     * plan's Phase 7.2. This only stops the desktop simulator from
     * staging that failure every time both products are run. */
    const char *config_store_path()
    {
        return SIM_HAS_BUTTONS ? "sim_config_gauge.bin" : "sim_config.bin";
    }
} // namespace

SimulatorConfigStore &SimulatorConfigStore::instance()
{
    static SimulatorConfigStore s_instance;
    return s_instance;
}

bool SimulatorConfigStore::read(uint32_t offset, void *buf, size_t len)
{
    if (offset + len > CONFIG_STORE_CAPACITY)
    {
        return false;
    }

    memset(buf, 0xFF, len);

    FILE *f = fopen(config_store_path(), "rb");
    if (!f)
    {
        return true; /* nothing written yet — report erased-flash default */
    }

    fseek(f, static_cast<long>(offset), SEEK_SET);
    fread(buf, 1, len, f);
    fclose(f);
    return true;
}

bool SimulatorConfigStore::write(uint32_t offset, const void *buf, size_t len)
{
    if (offset + len > CONFIG_STORE_CAPACITY)
    {
        return false;
    }

    FILE *f = fopen(config_store_path(), "r+b");
    if (!f)
    {
        f = fopen(config_store_path(), "w+b");
    }
    if (!f)
    {
        return false;
    }

    fseek(f, static_cast<long>(offset), SEEK_SET);
    bool ok = fwrite(buf, 1, len, f) == len;
    fclose(f);
    return ok;
}

size_t SimulatorConfigStore::capacity() const
{
    return CONFIG_STORE_CAPACITY;
}
