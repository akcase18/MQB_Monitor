/**
 * @file dash_s3_platform.cpp
 * @brief Composes the dash-on-ESP32-S3 board's Platform instance: the
 * MCU-level pieces from platform/esp32_common, the board-level ones from
 * this directory.
 */

#include "platform.hpp"

#include <esp_system.h>

#include "dash_s3_can.hpp"
#include "dash_s3_display.hpp"
#include "dash_s3_wireless.hpp"
#include "esp32_clock.hpp"
#include "esp32_config_store.hpp"
#include "esp32_storage.hpp"
#include "synthetic_can_bus.hpp"

/* Which ITouch backend is active. The product panel is a capacitive
 * FT6236 over I2C; the ER-TFTM035-6 breakout on the bench may instead
 * carry the resistive XPT2046 over SPI. Same pattern as the Teensy's
 * TEENSY_TOUCH_USE_RESISTIVE, except overridable with a -D from
 * platformio.ini like the pins are, since which panel is on the bench is
 * a wiring fact rather than a code decision. Both drivers compile
 * unconditionally either way, so the inactive one can't silently
 * bit-rot. */
#ifndef DASH_S3_TOUCH_USE_RESISTIVE
    #define DASH_S3_TOUCH_USE_RESISTIVE 0
#endif

#if DASH_S3_TOUCH_USE_RESISTIVE
    #include "dash_s3_res_touch.hpp"
    #define DASH_S3_ACTIVE_TOUCH DashS3ResTouch::instance()
#else
    #include "dash_s3_touch.hpp"
    #define DASH_S3_ACTIVE_TOUCH DashS3Touch::instance()
#endif

/* Which ICanBus is active. 0 = the TWAI controller (needs a transceiver
 * and a vehicle). 1 = the synthetic bus the simulator runs on, which
 * answers every UDS request instantly with a slowly drifting fabricated
 * value -- so the whole poll/decode/warn/render path runs flat out on a
 * bench with nothing but the panel attached. env:dash_s3_stress sets it. */
#ifndef DASH_S3_FAKE_CAN
    #define DASH_S3_FAKE_CAN 0
#endif

namespace
{
    ICanBus &active_can()
    {
#if DASH_S3_FAKE_CAN
        /* Step every response, half-range swing: each value sweeps its
         * whole triangle in ~512 responses, a few seconds at this poll
         * rate, so the panels visibly move and cross their thresholds.
         * The simulator's defaults (8, 256) are a gentle wobble tuned
         * for exercising the logger, and read as frozen on a bench. */
        static SyntheticCanBus s_bus(1, 2048);
        return s_bus;
#else
        return DashS3CanBus::instance();
#endif
    }
} // namespace

Platform &platform_get()
{
    static Platform s_platform {
        active_can(),                 DashS3Display::instance(),
        DASH_S3_ACTIVE_TOUCH,         Esp32ConfigStore::instance(),
        Esp32Clock::instance(),       DashS3Wireless::instance(),
        Esp32Storage::instance(),
    };
    return s_platform;
}

void platform_restart()
{
    esp_restart();
}
