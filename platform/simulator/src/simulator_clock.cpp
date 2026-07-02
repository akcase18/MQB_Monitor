/**
 * @file simulator_clock.cpp
 * @brief Phase 1 stub. Wired to SDL_GetTicks() in Phase 2.
 */

#include "simulator_clock.hpp"

SimulatorClock &SimulatorClock::instance()
{
    static SimulatorClock s_instance;
    return s_instance;
}

uint32_t SimulatorClock::millis() const
{
    return 0;
}
