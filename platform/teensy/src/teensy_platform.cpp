/**
 * @file teensy_platform.cpp
 * @brief Composes the Teensy board's Platform instance.
 */

#include "platform.hpp"
#include "teensy_can.hpp"
#include "teensy_clock.hpp"
#include "teensy_config_store.hpp"
#include "teensy_display.hpp"
#include "teensy_serial_link.hpp"

/* Which IStorage backend is active. No Micro-SD slot exists on the board
 * today -- flip this to 1 if/when one is added back. Only the active
 * backend's header is included below: SdFat.h and LittleFS.h both define
 * conflicting FILE_READ/FILE_WRITE macros (different numeric values), so
 * pulling in both within one translation unit isn't safe. Both
 * teensy_sd_card.cpp and teensy_storage.cpp still compile unconditionally
 * either way (see platformio.ini's teensymm build_src_filter), each in its
 * own isolated translation unit, so the inactive one can't silently
 * bit-rot. */
#define TEENSY_STORAGE_USE_SD 0

#if TEENSY_STORAGE_USE_SD
#include "teensy_sd_card.hpp"
#define TEENSY_ACTIVE_STORAGE TeensySdCard::instance()
#else
#include "teensy_storage.hpp"
#define TEENSY_ACTIVE_STORAGE TeensyStorage::instance()
#endif

/* Which ITouch backend is active. The new PCB uses a capacitive FT6236
 * over I2C -- flip this to 1 to go back to the resistive XPT2046. Only the
 * active backend's header is included below (SPI.h vs Wire.h aren't known
 * to collide the way SdFat/LittleFS did, but gating the include is cheap
 * and keeps the pattern consistent with TEENSY_STORAGE_USE_SD). Both
 * teensy_touch.cpp and teensy_cap_touch.cpp still compile unconditionally
 * either way, so the inactive one can't silently bit-rot. */
#define TEENSY_TOUCH_USE_RESISTIVE 0

#if TEENSY_TOUCH_USE_RESISTIVE
#include "teensy_touch.hpp"
#define TEENSY_ACTIVE_TOUCH TeensyTouch::instance()
#else
#include "teensy_cap_touch.hpp"
#define TEENSY_ACTIVE_TOUCH TeensyCapTouch::instance()
#endif

Platform &platform_get()
{
    static Platform s_platform {
        TeensyCanBus::instance(),    TeensyDisplay::instance(),
        TEENSY_ACTIVE_TOUCH,         TeensyConfigStore::instance(),
        TeensyClock::instance(),     TeensySerialLink::instance(),
        TEENSY_ACTIVE_STORAGE,
    };
    return s_platform;
}
