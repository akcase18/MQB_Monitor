/**
 * @file simulator_config_store.cpp
 * @brief IConfigStore backed by a local file, standing in for Teensy's
 * EEPROM. Unwritten regions read back as 0xFF to mirror erased-flash
 * behavior, so core/config's magic-number detection works the same on
 * both platforms.
 */

#include "simulator_config_store.hpp"

#include <stdio.h>
#include <string.h>

namespace
{
constexpr size_t     CONFIG_STORE_CAPACITY = 4096;
constexpr const char *CONFIG_STORE_PATH    = "sim_config.bin";
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

    FILE *f = fopen(CONFIG_STORE_PATH, "rb");
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

    FILE *f = fopen(CONFIG_STORE_PATH, "r+b");
    if (!f)
    {
        f = fopen(CONFIG_STORE_PATH, "w+b");
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
