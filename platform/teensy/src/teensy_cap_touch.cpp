/**
 * @file teensy_cap_touch.cpp
 * @brief ITouch over I2C for the FT6236, register layout per the standard
 * FocalTech FT6x06 family: TD_STATUS (0x02) holds the touch count in its
 * low nibble; point 1's X/Y follow at 0x03-0x06, each 12-bit value split
 * across a high byte (low nibble) and a low byte.
 */

#include "teensy_cap_touch.hpp"

#include <Arduino.h>
#include <Wire.h>

namespace
{
    constexpr uint8_t  FT6236_I2C_ADDR      = 0x38;
    constexpr uint8_t  FT6236_REG_TD_STATUS = 0x02;
    constexpr uint8_t  RST_PIN              = 33;
    constexpr uint32_t RESET_LOW_MS         = 10;
    constexpr uint32_t RESET_SETTLE_MS      = 300; /* conservative --
                                                      * datasheet only
                                                      * confirms Trst >
                                                      * 5ms low, not the
                                                      * post-release
                                                      * settle time */

    /* Must track teensy_display.cpp's LCD_HORZ/LCD_VERT -- duplicated
     * here rather than shared since ITouch::set_orientation() only passes
     * the orientation enum, not a resolution, same as the dormant
     * resistive driver (teensy_touch.hpp) already does. */
    constexpr uint16_t LCD_HORZ = 480;
    constexpr uint16_t LCD_VERT = 320;
} // namespace

TeensyCapTouch &TeensyCapTouch::instance()
{
    static TeensyCapTouch s_instance;
    return s_instance;
}

void TeensyCapTouch::set_orientation(DisplayOrientation orientation)
{
    orientation_ = orientation;
    height_ = orientation == DisplayOrientation::Portrait ? LCD_HORZ : LCD_VERT;
}

void TeensyCapTouch::begin()
{
    pinMode(RST_PIN, OUTPUT);
    digitalWrite(RST_PIN, LOW);
    delay(RESET_LOW_MS);
    digitalWrite(RST_PIN, HIGH);
    delay(RESET_SETTLE_MS);

    Wire.begin();
}

bool TeensyCapTouch::read(TouchPoint &out)
{
    uint8_t buf[5];
    Wire.beginTransmission(FT6236_I2C_ADDR);
    Wire.write(FT6236_REG_TD_STATUS);
    bool ok = Wire.endTransmission(false) == 0
           && Wire.requestFrom(FT6236_I2C_ADDR,
                                static_cast<uint8_t>(sizeof(buf)))
                  == sizeof(buf);

    bool pressed = false;
    if (ok)
    {
        for (uint8_t &b : buf)
        {
            b = static_cast<uint8_t>(Wire.read());
        }
        pressed = (buf[0] & 0x0F) > 0;
        if (pressed)
        {
            x_val_ = static_cast<int16_t>(((buf[1] & 0x0F) << 8) | buf[2]);
            y_val_ = static_cast<int16_t>(((buf[3] & 0x0F) << 8) | buf[4]);
        }
    }

    /* Last valid position retained across release -- see teensy_touch.cpp's
     * read() for why LVGL needs this (drag-release finalizes from this
     * exact point). */
    out.pressed = pressed;
    out.x       = x_val_;

    /* Not yet verified on real hardware -- this driver is currently
     * dormant (TEENSY_TOUCH_USE_RESISTIVE=1 in teensy_platform.cpp), so
     * this is inferred by analogy with teensy_touch.cpp's resistive
     * driver, which had the exact same symptom confirmed on real
     * hardware (bottom-right registers as top-right, top-left as
     * bottom-left, etc. -- raw X already correct, raw Y mirrored) once
     * the display's own Portrait rotation was corrected. Needs its own
     * hardware confirmation once this driver is switched active --
     * see TODO.md. Landscape is untouched here (zero-transform) and is
     * equally unverified on this driver -- the resistive driver's own
     * Landscape mapping is confirmed good, but that doesn't carry over
     * to this dormant driver. */
    out.y = orientation_ == DisplayOrientation::Portrait
                ? (height_ - 1) - y_val_
                : y_val_;

    return pressed;
}
