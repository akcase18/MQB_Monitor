/**
 * @file simulator_can.cpp
 * @brief Phase 1 stub. Synthetic/mock CAN source lands in Phase 2.
 */

#include "simulator_can.hpp"

SimulatorCanBus &SimulatorCanBus::instance()
{
    static SimulatorCanBus s_instance;
    return s_instance;
}

bool SimulatorCanBus::begin(uint32_t tx_id, uint32_t rx_id, uint32_t baud)
{
    return false;
}

bool SimulatorCanBus::send(const CanFrame &frame)
{
    return false;
}

bool SimulatorCanBus::receive(CanFrame &out)
{
    return false;
}
