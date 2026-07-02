/**
 * @file teensy_can.cpp
 * @brief FlexCAN_T4 wiring behind ICanBus. CAN3, 16-deep RX/TX ring buffers
 * match the old can_handler.cpp setup. Response filtering by CAN ID is left
 * to the caller (core/can in a later phase) — this layer only moves frames.
 */

#include "teensy_can.hpp"

#include <FlexCAN_T4.h>

namespace
{
    FlexCAN_T4<CAN3, RX_SIZE_16, TX_SIZE_16> s_bus;
}

TeensyCanBus &TeensyCanBus::instance()
{
    static TeensyCanBus s_instance;
    return s_instance;
}

bool TeensyCanBus::begin(uint32_t tx_id, uint32_t rx_id, uint32_t baud)
{
    (void)tx_id;

    s_bus.begin();
    s_bus.setBaudRate(baud);
    s_bus.setMBFilter(REJECT_ALL);
    s_bus.setMBFilter(MB1, rx_id);
    return true;
}

bool TeensyCanBus::send(const CanFrame &frame)
{
    CAN_message_t msg;
    msg.id  = frame.id;
    msg.len = frame.len;
    for (uint8_t i = 0; i < frame.len && i < sizeof(msg.buf); i++)
    {
        msg.buf[i] = frame.data[i];
    }

    return s_bus.write(msg) > 0;
}

bool TeensyCanBus::receive(CanFrame &out)
{
    CAN_message_t msg;
    if (s_bus.read(msg) <= 0)
    {
        return false;
    }

    out.id  = msg.id;
    out.len = msg.len;
    for (uint8_t i = 0; i < msg.len && i < sizeof(out.data); i++)
    {
        out.data[i] = msg.buf[i];
    }

    return true;
}
