/**
 * @file teensy_clock.cpp
 * @brief IClock over Arduino millis().
 */

#include "teensy_clock.hpp"

#include <Arduino.h>

TeensyClock &TeensyClock::instance()
{
    static TeensyClock s_instance;
    return s_instance;
}

uint32_t TeensyClock::millis() const
{
    return ::millis();
}
