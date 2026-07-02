/**
 * @file simulator_clock.hpp
 * @brief Simulator IClock implementation, wired to SDL_GetTicks().
 */
#pragma once

#include "platform.hpp"

class SimulatorClock : public IClock
{
    public:
        static SimulatorClock &instance();

        uint32_t millis() const override;

    private:
        SimulatorClock() = default;
};
