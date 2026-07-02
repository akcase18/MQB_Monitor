/**
 * @file simulator_config_store.cpp
 * @brief Phase 1 stub. Local file-backed store lands in Phase 2.
 */

#include "simulator_config_store.hpp"

SimulatorConfigStore &SimulatorConfigStore::instance()
{
    static SimulatorConfigStore s_instance;
    return s_instance;
}

bool SimulatorConfigStore::read(uint32_t offset, void *buf, size_t len)
{
    return false;
}

bool SimulatorConfigStore::write(uint32_t offset, const void *buf, size_t len)
{
    return false;
}

size_t SimulatorConfigStore::capacity() const
{
    return 0;
}
