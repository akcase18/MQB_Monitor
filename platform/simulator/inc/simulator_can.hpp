/**
 * @file simulator_can.hpp
 * @brief The simulator's ICanBus: the board-agnostic SyntheticCanBus
 * (platform/synthetic_can_bus.hpp) as a singleton. The fabricated-response
 * logic used to live here; it moved up a level once a real board wanted
 * the same thing for bench testing.
 */
#pragma once

#include "synthetic_can_bus.hpp"

class SimulatorCanBus : public SyntheticCanBus
{
    public:
        static SimulatorCanBus &instance();

    private:
        SimulatorCanBus() = default;
};
