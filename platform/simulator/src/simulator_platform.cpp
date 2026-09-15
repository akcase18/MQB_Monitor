/**
 * @file simulator_platform.cpp
 * @brief Composes the simulator's Platform instance.
 */

#include "null_analog_input.hpp"
#include "null_buttons.hpp"
#include "platform.hpp"
#include "simulator_analog_input.hpp"
#include "simulator_buttons.hpp"
#include "simulator_can.hpp"
#include "simulator_config.hpp"
#include "simulator_clock.hpp"
#include "simulator_config_store.hpp"
#include "simulator_display.hpp"
#include "simulator_serial_link.hpp"
#include "simulator_storage.hpp"
#include "simulator_touch.hpp"

/**
 * A touchscreen product has no buttons to emulate; a buttonless one needs
 * them to be reachable at all. See simulator_config.hpp. */
#if SIM_HAS_BUTTONS
    #define SIM_ACTIVE_BUTTONS SimulatorButtons::instance()
#else
    #define SIM_ACTIVE_BUTTONS NullButtons::instance()
#endif

#if SIM_HAS_ANALOG
    #define SIM_ACTIVE_ANALOG SimulatorAnalogInput::instance()
#else
    #define SIM_ACTIVE_ANALOG NullAnalogInput::instance()
#endif

Platform &platform_get()
{
    static Platform s_platform {
        SimulatorCanBus::instance(),      SimulatorDisplay::instance(),
        SimulatorTouch::instance(),       SIM_ACTIVE_BUTTONS,
        SIM_ACTIVE_ANALOG,                SimulatorConfigStore::instance(),
        SimulatorClock::instance(),       SimulatorSerialLink::instance(),
        SimulatorStorage::instance(),
    };
    return s_platform;
}
