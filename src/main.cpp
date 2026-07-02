/**
 * @file main.cpp
 * @brief Composition root. Builds the target board's Platform and (in
 * later phases) wires up core/gui objects on top of it. This is the only
 * file that references a concrete platform_get() implementation, and only
 * one implementation is ever linked per build (see platformio.ini).
 *
 * Phase 2 smoke test: fills a small square via IDisplay::flush() and
 * recolors it on ITouch press/release, to validate both HALs end to end
 * before any core/gui code exists. Replaced by real composition in Phase 7.
 */

#include "platform.hpp"

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace
{
constexpr int32_t SWATCH_SIZE = 40;

constexpr uint16_t COLOR_IDLE     = 0x001F; /* blue,  RGB565 */
constexpr uint16_t COLOR_PRESSED  = 0x07E0; /* green, RGB565 */

uint16_t s_swatch[SWATCH_SIZE * SWATCH_SIZE];

void fill_swatch(uint16_t color)
{
    for (auto &pixel : s_swatch)
    {
        pixel = color;
    }
}
} // namespace

void setup()
{
    Platform &platform = platform_get();

    platform.display.begin();
    platform.touch.begin();

    fill_swatch(COLOR_IDLE);
    platform.display.flush(
        0, 0, SWATCH_SIZE - 1, SWATCH_SIZE - 1, s_swatch);
}

void loop()
{
    Platform &platform = platform_get();

    static bool was_pressed = false;

    TouchPoint point{};
    bool       pressed = platform.touch.read(point);

    if (pressed != was_pressed)
    {
        fill_swatch(pressed ? COLOR_PRESSED : COLOR_IDLE);
        platform.display.flush(
            0, 0, SWATCH_SIZE - 1, SWATCH_SIZE - 1, s_swatch);
        was_pressed = pressed;
    }
}

#if !defined(ARDUINO) && !defined(UNIT_TEST)
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
