/**
 * @file teensy_display.cpp
 * @brief Phase 1 stub. ILI948x_t4_mm wiring lands in Phase 2.
 */

#include "teensy_display.hpp"

TeensyDisplay &TeensyDisplay::instance()
{
    static TeensyDisplay s_instance;
    return s_instance;
}

void TeensyDisplay::begin()
{
}

void TeensyDisplay::get_resolution(uint16_t &width, uint16_t &height) const
{
    width  = 0;
    height = 0;
}

void TeensyDisplay::flush(int32_t         x1,
                          int32_t         y1,
                          int32_t         x2,
                          int32_t         y2,
                          const uint16_t *pixels)
{
}

bool TeensyDisplay::flush_ready() const
{
    return true;
}

void TeensyDisplay::set_backlight(uint8_t pct)
{
}
