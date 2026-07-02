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
    s_lcd.begin(12);
    s_lcd.setBitDepth(16);
    s_lcd.setRotation(1);

    pinMode(PWM_PIN, OUTPUT);
    analogWrite(PWM_PIN, 255);
}

void TeensyDisplay::get_resolution(uint16_t &width, uint16_t &height) const
{
    width  = LCD_HORZ;
    height = LCD_VERT;
}

void TeensyDisplay::flush(int32_t         x1,
                          int32_t         y1,
                          int32_t         x2,
                          int32_t         y2,
                          const uint16_t *pixels)
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
    analogWrite(PWM_PIN, (static_cast<uint16_t>(pct) * 255) / 100);
}
