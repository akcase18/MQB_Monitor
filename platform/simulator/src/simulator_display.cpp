/**
 * @file simulator_display.cpp
 * @brief Phase 1 stub. Raw SDL2 window/texture wiring lands in Phase 2.
 */

#include "simulator_display.hpp"

SimulatorDisplay &SimulatorDisplay::instance()
{
    static SimulatorDisplay s_instance;
    return s_instance;
}

void SimulatorDisplay::begin()
{
}

void SimulatorDisplay::get_resolution(uint16_t &width, uint16_t &height) const
{
    width  = 0;
    height = 0;
}

void SimulatorDisplay::flush(int32_t         x1,
                             int32_t         y1,
                             int32_t         x2,
                             int32_t         y2,
                             const uint16_t *pixels)
{
}

bool SimulatorDisplay::flush_ready() const
{
    return true;
}

void SimulatorDisplay::set_backlight(uint8_t pct)
{
}
