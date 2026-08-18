/**
 * @file teensy_display.cpp
 * @brief ILI948x_t4_mm wiring behind IDisplay. Pins/resolution/backlight
 * ported from the old main.cpp / Screen.hpp.
 */

#include "teensy_display.hpp"

#include <Arduino.h>
#include <ILI948x_t4_mm.h>

namespace
{
    /* D0 */
    constexpr int8_t LCD_CS = 4;
    /* D1 */
    constexpr int8_t LCD_DC = 5;
    /* G9 */
    constexpr int8_t LCD_RST = 32;

    constexpr uint16_t LCD_HORZ = 480;
    constexpr uint16_t LCD_VERT = 320;

    constexpr uint8_t PWM_PIN = 3;

    ILI948x_t4_mm s_lcd(LCD_DC, LCD_CS, LCD_RST);
} // namespace

TeensyDisplay &TeensyDisplay::instance()
{
    static TeensyDisplay s_instance;
    return s_instance;
}

void TeensyDisplay::begin()
{
    /* Drive the backlight pin low before touching the panel at all —
     * pinMode() defaults to INPUT at boot, which leaves it floating (not
     * actually off) for however long s_lcd.begin()'s reset/init sequence
     * takes, so whatever's left over in GRAM from power-on can flash
     * through if there's a pull-up on the gate. Actively holding it low
     * first closes that window. Stays low after init too — the panel's
     * GRAM is still stale/uninitialized until the first real frame is
     * flushed, so it's on the caller (see main.cpp's display_flush_cb) to
     * turn the backlight on once that's actually happened. */
    pinMode(PWM_PIN, OUTPUT);
    digitalWrite(PWM_PIN, LOW);

    s_lcd.begin(12);
    s_lcd.setBitDepth(16);
    s_lcd.setRotation(3);
}

void TeensyDisplay::get_resolution(uint16_t &width, uint16_t &height) const
{
    width  = LCD_HORZ;
    height = LCD_VERT;
}

void TeensyDisplay::flush(
    int32_t x1, int32_t y1, int32_t x2, int32_t y2, const uint16_t *pixels)
{
    /* pushPixels16bitDMA blocks internally until any prior transfer has
     * completed before issuing this one, so flush_ready() can just report
     * true unconditionally. */
    s_lcd.pushPixels16bitDMA(pixels, x1, y1, x2, y2);
}

bool TeensyDisplay::flush_ready() const
{
    return true;
}

void TeensyDisplay::set_backlight(uint8_t pct)
{
    if (pct > 100)
    {
        pct = 100;
    }

    /* Below ~15% duty the panel reads as fully black rather than dim, so
     * the user-facing 0-100% range maps onto an actual
     * MIN_BACKLIGHT_PCT-100% duty cycle instead of 0-100% — 0% is "as dim
     * as it gets" rather than "off". */
    constexpr uint32_t MIN_BACKLIGHT_PCT = 15;
    uint32_t           effective_pct
        = MIN_BACKLIGHT_PCT
          + (static_cast<uint32_t>(pct) * (100 - MIN_BACKLIGHT_PCT)) / 100;

    analogWrite(PWM_PIN, (effective_pct * 255) / 100);
}
