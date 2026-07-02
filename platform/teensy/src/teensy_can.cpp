/**
 * @file teensy_can.cpp
 * @brief Phase 1 stub. FlexCAN_T4 wiring lands in Phase 2.
 */

#include "teensy_can.hpp"

TeensyCanBus &TeensyCanBus::instance()
{
    static TeensyCanBus s_instance;
    return s_instance;
}

bool TeensyCanBus::begin(uint32_t tx_id, uint32_t rx_id, uint32_t baud)
{
    return false;
}

bool TeensyCanBus::send(const CanFrame &frame)
{
    return false;
}

bool TeensyCanBus::receive(CanFrame &out)
{
    return false;
}
