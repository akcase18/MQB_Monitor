#include "esp32_config_store.hpp"

#include <EEPROM.h>
#include <string.h>

Esp32ConfigStore &Esp32ConfigStore::instance()
{
    static Esp32ConfigStore s_instance;
    return s_instance;
}

bool Esp32ConfigStore::begin()
{
    if (began_)
    {
        return true;
    }

    began_ = EEPROM.begin(CAPACITY_BYTES);
    return began_;
}

bool Esp32ConfigStore::read(uint32_t offset, void *buf, size_t len)
{
    /**
     * Opened on demand. IConfigStore has no begin() -- the Teensy's real
     * EEPROM needs none -- and adding one to the interface for this MCU
     * alone would make every other implementation carry an empty method.
     * So the region opens itself on first use, which is the same trick
     * Esp32Storage uses for its mount.
     */
    begin();

    if (!began_ || buf == nullptr || (offset + len) > CAPACITY_BYTES)
    {
        return false;
    }

    EEPROM.readBytes(offset, buf, len);
    return true;
}

bool Esp32ConfigStore::write(uint32_t offset, const void *buf, size_t len)
{
    begin();

    if (!began_ || buf == nullptr || (offset + len) > CAPACITY_BYTES)
    {
        return false;
    }

    EEPROM.writeBytes(offset, buf, len);

    /**
     * Committed here rather than batched -- see the header. IConfigStore
     * has no flush, so a caller is entitled to assume durability on
     * return.
     */
    return EEPROM.commit();
}

size_t Esp32ConfigStore::capacity() const
{
    return CAPACITY_BYTES;
}
