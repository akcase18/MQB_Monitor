/**
 * @file simulator_touch.cpp
 * @brief Phase 1 stub. SDL2 mouse polling wiring lands in Phase 2.
 */

#include "simulator_touch.hpp"

SimulatorTouch &SimulatorTouch::instance()
{
    static SimulatorTouch s_instance;
    return s_instance;
}

void SimulatorTouch::begin()
{
}

bool SimulatorTouch::read(TouchPoint &out)
{
    out.x       = 0;
    out.y       = 0;
    out.pressed = false;
    return false;
}
