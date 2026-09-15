#include "esp32s3_clock.hpp"

#include <Arduino.h>

Esp32s3Clock &Esp32s3Clock::instance()
{
    static Esp32s3Clock s_instance;
    return s_instance;
}

uint32_t Esp32s3Clock::millis() const
{
    /* Qualified, because this class has a member of the same name. */
    return ::millis();
}
