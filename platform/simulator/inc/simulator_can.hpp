/**
 * @file simulator_can.hpp
 * @brief Synthetic ICanBus for the simulator: send() immediately queues a
 * fabricated UDS response for the next receive(), standing in for a real
 * ECU so core/can + core/vehicle (and later gui/) can be exercised without
 * real hardware.
 */
#pragma once

#include "platform.hpp"

class SimulatorCanBus : public ICanBus
{
    public:
        static SimulatorCanBus &instance();

        bool begin(uint32_t tx_id, uint32_t rx_id, uint32_t baud) override;
        bool send(const CanFrame &frame) override;
        bool receive(CanFrame &out) override;

        /** The raw value the most recent send() fabricated a response
         * with. Exists for tests: fabricated values include a slow drift
         * (see simulator_can.cpp), so a test can no longer recompute the
         * expected value from a fixed formula and must assert against
         * what was actually sent. */
        uint16_t last_fake_raw() const
        {
            return last_fake_raw_;
        }

    private:
        SimulatorCanBus() = default;

        static constexpr uint8_t QUEUE_SIZE = 8;

        CanFrame queue_[QUEUE_SIZE] = {};
        uint8_t  head_              = 0;
        uint8_t  tail_              = 0;
        uint8_t  count_             = 0;

        /* Monotonic response counter driving the slow value drift in
         * send() -- see there for why fabricated values can't be
         * constant. */
        uint32_t tick_          = 0;
        uint16_t last_fake_raw_ = 0;
        uint32_t rx_id_             = 0;
};
