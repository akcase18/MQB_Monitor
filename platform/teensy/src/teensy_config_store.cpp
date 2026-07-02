/**
 * @file teensy_config_store.cpp
 * @brief Phase 1 stub. EEPROM.h wiring lands in Phase 2.
 */

#include "teensy_config_store.hpp"

TeensyConfigStore &TeensyConfigStore::instance()
{
    static TeensyConfigStore s_instance;
    return s_instance;
}

bool TeensyConfigStore::read(uint32_t offset, void *buf, size_t len)
{
    return false;
}

bool TeensyConfigStore::write(uint32_t offset, const void *buf, size_t len)
{
    return false;
}

size_t TeensyConfigStore::capacity() const
{
    return 0;
}
