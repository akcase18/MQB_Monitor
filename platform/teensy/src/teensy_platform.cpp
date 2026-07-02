/**
 * @file teensy_platform.cpp
 * @brief Composes the Teensy board's Platform instance.
 */

#include "platform.hpp"
#include "teensy_can.hpp"
#include "teensy_clock.hpp"
#include "teensy_config_store.hpp"
#include "teensy_display.hpp"
#include "teensy_touch.hpp"

Platform &platform_get()
{
    static Platform s_platform{
        TeensyCanBus::instance(),
        TeensyDisplay::instance(),
        TeensyTouch::instance(),
        TeensyConfigStore::instance(),
        TeensyClock::instance(),
    };
    return s_platform;
}
