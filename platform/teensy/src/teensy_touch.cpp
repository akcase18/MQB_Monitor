/**
 * @file teensy_touch.cpp
 * @brief Phase 1 stub. Raw SPI touch controller wiring lands in Phase 2.
 */

#include "teensy_touch.hpp"

TeensyTouch &TeensyTouch::instance()
{
    static TeensyTouch s_instance;
    return s_instance;
}

void TeensyTouch::begin()
{
}

bool TeensyTouch::read(TouchPoint &out)
{
    out.x       = 0;
    out.y       = 0;
    out.pressed = false;
    return false;
}
