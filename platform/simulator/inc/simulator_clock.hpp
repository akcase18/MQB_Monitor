/**
 * @file simulator_clock.hpp
 * @brief Simulator IClock implementation. Phase 1 stub — wired to
 * SDL_GetTicks() in Phase 2.
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
