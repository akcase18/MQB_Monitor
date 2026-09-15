/**
 * @file esp32s3_platform.cpp
 * @brief platform_get() for the analog gauge board.
 *
 * Three of the nine members are nulls, and that is the honest description
 * of this hardware rather than a gap:
 *
 * - **No CAN.** The target vehicle has no bus to read; three analog
 *   sensors replace it. That is the product's premise.
 * - **No touch.** Two buttons and one screen. src/main_gauge.cpp
 *   consequently builds LVGL with no pointer input device at all, which is
 *   the largest simplification the touchless design buys.
 * - **No serial link.** The CAN product reaches a phone through a
 *   companion ESP32-C3 on a UART. This chip's Wi-Fi is on-die, so there is
 *   no second processor and no wire to it.
 */
#include "platform.hpp"

#include "null_analog_input.hpp"
#include "null_buttons.hpp"
#include "null_can_bus.hpp"
#include "null_serial_link.hpp"
#include "null_touch.hpp"

#include "esp32s3_analog_input.hpp"
#include "esp32s3_buttons.hpp"
#include "esp32s3_clock.hpp"
#include "esp32s3_config_store.hpp"
#include "esp32s3_display.hpp"
#include "esp32s3_storage.hpp"

Platform &platform_get()
{
    static Platform s_platform {
        NullCanBus::instance(),
        Esp32s3Display::instance(),
        NullTouch::instance(),
        Esp32s3Buttons::instance(),
        Esp32s3AnalogInput::instance(),
        Esp32s3ConfigStore::instance(),
        Esp32s3Clock::instance(),
        NullSerialLink::instance(),
        Esp32s3Storage::instance(),
    };
    return s_platform;
}
