/**
 * @file main.cpp
 * @brief Composition root. Builds the target board's Platform and (in
 * later phases) wires up core/gui objects on top of it. This is the only
 * file that references a concrete platform_get() implementation, and only
 * one implementation is ever linked per build (see platformio.ini).
 */

#include "platform.hpp"

#ifdef ARDUINO
#include <Arduino.h>
#endif

void setup()
{
    Platform &platform = platform_get();
    (void)platform;
}

void loop()
{
}

#ifndef ARDUINO
int main()
{
    setup();

    for (;;)
    {
        loop();
    }

    return 0;
}
#endif
