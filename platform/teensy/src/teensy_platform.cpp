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
#include "teensy_touch.hpp"

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

Platform &platform_get()
{
    static Platform s_platform {
        TeensyCanBus::instance(), TeensyDisplay::instance(),
        TeensyTouch::instance(),  TeensyConfigStore::instance(),
        TeensyClock::instance(),  TeensySerialLink::instance(),
        TEENSY_ACTIVE_STORAGE,
    };
    return s_platform;
}
