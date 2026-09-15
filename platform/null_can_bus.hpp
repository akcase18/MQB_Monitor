/**
 * @file null_can_bus.hpp
 * @brief ICanBus for a board with no vehicle bus.
 *
 * Platform's members are references, so every board must supply one of
 * everything even when the hardware isn't there -- the same reason
 * NullButtons and NullAnalogInput exist.
 *
 * begin() reports failure rather than success, and send() refuses. A
 * composition root that has no business talking to a bus never calls
 * these; one that does gets an honest "there is no bus here" instead of a
 * silent black hole that looks like an ECU which never answers.
 *
 * Header-only and board-agnostic: "this board has no CAN" is not a
 * property of any particular board.
 */
#pragma once

#include "platform.hpp"

class NullCanBus : public ICanBus
{
    public:
        static NullCanBus &instance()
        {
            static NullCanBus s_instance;
            return s_instance;
        }

        bool begin(uint32_t tx_id, uint32_t rx_id, uint32_t baud) override
        {
            (void)tx_id;
            (void)rx_id;
            (void)baud;
            return false;
        }

        bool send(const CanFrame &frame) override
        {
            (void)frame;
            return false;
        }

        bool receive(CanFrame &out) override
        {
            (void)out;
            return false;
        }
};
