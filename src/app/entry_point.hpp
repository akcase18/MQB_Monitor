/**
 * @file entry_point.hpp
 * @brief setup() and loop(), declared with the linkage each host expects.
 *
 * A composition root that simply defines `void setup()` in C++ builds
 * perfectly on the desktop and then fails to link on hardware, with an
 * error that reads like a missing file rather than like a linkage
 * problem. Declaring them here, once, is what lets both roots carry no
 * preprocessor branches at all -- the branch is here instead, in the file
 * whose entire job is the entry point.
 *
 * **And the two Arduino cores disagree about it**, which is the part
 * worth knowing before adding a third board:
 *
 * | Core | Declares | Links against |
 * |---|---|---|
 * | Teensyduino | `extern "C"` (cores/teensy4/wiring.h, via Arduino.h) | C-mangled `setup`/`loop` |
 * | Arduino-ESP32 | plain C++ (cores/esp32/Arduino.h) | C++-mangled |
 *
 * Getting it backwards fails exactly the same way in either direction, so
 * the branch below is not defensive -- it is load-bearing on both boards.
 *
 * The alternative, including `<Arduino.h>` here and letting each core
 * declare them itself, is rejected on purpose: Teensyduino's Arduino.h
 * defines `abs` and friends as macros, and dragging that into every
 * composition root ahead of `<lvgl.h>` and the C++ headers is what this
 * file was created to avoid.
 *
 * This branches on the toolchain's own defines (`ARDUINO`,
 * `ARDUINO_ARCH_ESP32`), never on the project's `PLATFORM_*` ones -- the
 * same distinction platform/mem_placement.hpp already draws.
 *
 * entry_point.cpp supplies the `main()` that stands in for the Arduino
 * core's off-hardware.
 */
#pragma once

#if defined(ARDUINO_ARCH_ESP32)
void setup(void);
void loop(void);
#elif defined(ARDUINO)
extern "C"
{
    void setup(void);
    void loop(void);
}
#else
void setup();
void loop();
#endif
