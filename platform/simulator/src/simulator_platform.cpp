/**
 * @file simulator_platform.cpp
 * @brief Composes the simulator's Platform instance.
 */

#include "platform.hpp"
#include "simulator_can.hpp"
#include "simulator_clock.hpp"
#include "simulator_config_store.hpp"
#include "simulator_display.hpp"
#include "simulator_serial_link.hpp"
#include "simulator_touch.hpp"

Platform &platform_get()
{
    static Platform s_platform {
        SimulatorCanBus::instance(), SimulatorDisplay::instance(),
        SimulatorTouch::instance(),  SimulatorConfigStore::instance(),
        SimulatorClock::instance(),  SimulatorSerialLink::instance(),
    };
    return s_platform;
}
