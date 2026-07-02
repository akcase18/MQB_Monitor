/**
 * @file teensy_clock.cpp
 * @brief Phase 1 stub. Wired to Arduino millis() in Phase 2.
 */

#include "teensy_clock.hpp"

TeensyClock &TeensyClock::instance()
{
    static TeensyClock s_instance;
    return s_instance;
}

uint32_t TeensyClock::millis() const
{
    return 0;
}
