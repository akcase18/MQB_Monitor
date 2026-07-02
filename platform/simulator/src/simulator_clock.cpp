/**
 * @file simulator_clock.cpp
 * @brief IClock over SDL_GetTicks().
 */

#include "simulator_clock.hpp"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

SimulatorClock &SimulatorClock::instance()
{
    static SimulatorClock s_instance;
    return s_instance;
}

uint32_t SimulatorClock::millis() const
{
    return SDL_GetTicks();
}
