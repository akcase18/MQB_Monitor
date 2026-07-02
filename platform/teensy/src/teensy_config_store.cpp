/**
 * @file teensy_config_store.cpp
 * @brief IConfigStore over Teensy's EEPROM.h. Byte-by-byte with
 * EEPROM.update() so unchanged bytes don't cost a write cycle.
 */

#include "teensy_config_store.hpp"

#include <EEPROM.h>

TeensyConfigStore &TeensyConfigStore::instance()
{
    static TeensyConfigStore s_instance;
    return s_instance;
}

bool TeensyConfigStore::read(uint32_t offset, void *buf, size_t len)
{
    if (offset + len > capacity())
    {
        return false;
    }

    uint8_t *dst = static_cast<uint8_t *>(buf);
    for (size_t i = 0; i < len; i++)
    {
        dst[i] = EEPROM.read(offset + i);
    }
    return true;
}

bool TeensyConfigStore::write(uint32_t offset, const void *buf, size_t len)
{
    if (offset + len > capacity())
    {
        return false;
    }

    const uint8_t *src = static_cast<const uint8_t *>(buf);
    for (size_t i = 0; i < len; i++)
    {
        EEPROM.update(offset + i, src[i]);
    }
    return true;
}

size_t TeensyConfigStore::capacity() const
{
    return EEPROM.length();
}
