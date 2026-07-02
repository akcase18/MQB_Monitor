/**
 * @file simulator_can.hpp
 * @brief Simulator ICanBus implementation. Phase 1 stub — a synthetic
 * loopback/mock source lands in Phase 2 (there's no real CAN bus on host).
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

    private:
        SimulatorCanBus() = default;
};
