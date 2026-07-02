/**
 * @file teensy_can.hpp
 * @brief Teensy ICanBus implementation, backed by FlexCAN_T4.
 */
#pragma once

#include "platform.hpp"

class TeensyCanBus : public ICanBus
{
    public:
        static TeensyCanBus &instance();

        bool begin(uint32_t tx_id, uint32_t rx_id, uint32_t baud) override;
        bool send(const CanFrame &frame) override;
        bool receive(CanFrame &out) override;

    private:
        TeensyCanBus() = default;
};
