#include "esp32_clock.hpp"

#include <Arduino.h>

Esp32Clock &Esp32Clock::instance()
{
    static Esp32Clock s_instance;
    return s_instance;
}

uint32_t Esp32Clock::millis() const
{
    /* Qualified, because this class has a member of the same name. */
    return ::millis();
}
