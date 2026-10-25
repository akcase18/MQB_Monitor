/**
 * @file dash_s3_touch.cpp
 * @brief ITouch over I2C for the FT6236, register layout per the standard
 * FocalTech FT6x06 family: TD_STATUS (0x02) holds the touch count in its
 * low nibble; point 1's X/Y follow at 0x03-0x06, each 12-bit value split
 * across a high byte (low nibble) and a low byte.
 *
 * Ported from platform/teensy/src/teensy_cap_touch.cpp. The coordinate
 * transform is the same because the panel is the same; see read().
 */

#include "dash_s3_touch.hpp"

#include <Arduino.h>
#include <Wire.h>

#include "dash_s3_pins.hpp"

namespace
{
    constexpr uint8_t FT6236_I2C_ADDR      = 0x38;
    constexpr uint8_t FT6236_REG_TD_STATUS = 0x02;

    /* Touch-detection threshold: how hard a press has to be before the
     * controller reports it. LOWER = lighter touches register.
     *
     * IMPORTANT -- the register address here is NOT confirmed against a
     * datasheet. docs/CapacitiveTouchDatasheet.pdf is FocalTech's
     * electrical/mechanical datasheet and contains no register map at all
     * (verified: no hex addresses anywhere in it); the FT6x06 register map
     * is in a separate application note we do not have. 0x80 as the
     * threshold register is from general knowledge of the FT6x06 family,
     * so begin() reads it back and logs it rather than trusting it.
     *
     * A plausible value on the read (not 0x00, not 0xFF) is evidence the
     * register exists and tells you the factory default to move away from.
     * All-zeroes or all-ones means the address is wrong and nothing here
     * should be trusted -- leave THRESHOLD at 0 in that case.
     *
     * Set FT6236_THRESHOLD to 0 to leave the controller's default alone.
     * The controller holds this in RAM, not flash, so it is re-applied on
     * every boot by design. */
    constexpr uint8_t  FT6236_REG_THRESHOLD = 0x80;
    constexpr uint8_t  FT6236_THRESHOLD     = 0; /* 0 = leave default */
    constexpr uint32_t RESET_LOW_MS         = 10;
    constexpr uint32_t RESET_SETTLE_MS      = 300; /* conservative --
                                                      * datasheet only
                                                      * confirms Trst >
                                                      * 5ms low, not the
                                                      * post-release
                                                      * settle time */

    constexpr uint32_t I2C_FREQ_HZ = 400000;

    /* Arduino-ESP32's Wire defaults to a 50ms timeout per transaction.
     * read() runs every 10ms from LVGL's indev timer, so a controller
     * that is unplugged, unpowered or wedged would stall the UI for five
     * times longer than the poll period on every poll. A 5-byte read at
     * 400kHz is ~150us; 5ms is thirty times that. */
    constexpr uint16_t I2C_TIMEOUT_MS = 5;

    /* The panel's LONG edge, and the full span of the controller's raw Y
     * axis. Both orientations invert against this same 480 because raw Y
     * always runs along the long edge, whichever way the display is
     * rotated -- see read(). Must track dash_s3_display.cpp's LCD_HORZ;
     * duplicated here rather than shared since ITouch::set_orientation()
     * doesn't pass a resolution. */
    constexpr uint16_t LCD_HORZ = 480;
    /* The short edge -- only needed to rotate a point 180 degrees within
     * the screen (see flip_touch_point()). */
    constexpr uint16_t LCD_VERT = 320;
} // namespace

DashS3Touch &DashS3Touch::instance()
{
    static DashS3Touch s_instance;
    return s_instance;
}

void DashS3Touch::set_orientation(DisplayOrientation orientation,
                                 bool               flipped)
{
    orientation_ = orientation;
    flipped_     = flipped;
}

bool DashS3Touch::read_reg(uint8_t reg, uint8_t &val)
{
    Wire.beginTransmission(FT6236_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0)
    {
        return false;
    }
    if (Wire.requestFrom(FT6236_I2C_ADDR, static_cast<uint8_t>(1)) != 1)
    {
        return false;
    }
    val = static_cast<uint8_t>(Wire.read());
    return true;
}

void DashS3Touch::write_reg(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(FT6236_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

void DashS3Touch::begin()
{
    pinMode(DASH_S3_PIN_TOUCH_RST, OUTPUT);
    digitalWrite(DASH_S3_PIN_TOUCH_RST, LOW);
    delay(RESET_LOW_MS);
    digitalWrite(DASH_S3_PIN_TOUCH_RST, HIGH);
    delay(RESET_SETTLE_MS);

    Wire.begin(DASH_S3_PIN_TOUCH_SDA, DASH_S3_PIN_TOUCH_SCL, I2C_FREQ_HZ);
    Wire.setTimeOut(I2C_TIMEOUT_MS);

    /* Read the threshold back before touching it -- see FT6236_REG_THRESHOLD.
     * This is the only evidence we have that the address is real. It
     * doubles as the presence probe: on a bench with no touch panel
     * wired, read() must not spend every poll timing out on the bus. */
    uint8_t before = 0;
    present_       = read_reg(FT6236_REG_THRESHOLD, before);
    if (present_)
    {
        Serial.printf("FT6236 threshold reg 0x%02X reads 0x%02X\n",
                      FT6236_REG_THRESHOLD,
                      before);
    }
    else
    {
        Serial.println("FT6236 threshold read FAILED (I2C) -- touch disabled");
    }

    if (present_ && FT6236_THRESHOLD != 0)
    {
        write_reg(FT6236_REG_THRESHOLD, FT6236_THRESHOLD);

        uint8_t after = 0;
        if (read_reg(FT6236_REG_THRESHOLD, after))
        {
            Serial.printf("FT6236 threshold set to 0x%02X, reads back 0x%02X%s\n",
                          FT6236_THRESHOLD,
                          after,
                          after == FT6236_THRESHOLD ? "" : "  <-- MISMATCH");
        }
    }
}

bool DashS3Touch::read(TouchPoint &out)
{
    if (!present_)
    {
        out.pressed = false;
        out.x       = x_val_;
        out.y       = y_val_;
        return false;
    }

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

    /* Last valid position retained across release -- LVGL finalizes a
     * drag-release from this exact point. */
    out.pressed = pressed;

    /* The FT6236 reports in the panel's NATIVE portrait frame regardless of
     * how the display is rotated: raw X spans 0-319 (the panel's short
     * edge), raw Y spans 0-479 (the long edge).
     *
     * Both orientations were MEASURED on this panel, on the Teensy -- see
     * teensy_cap_touch.cpp for the five-point tables. The display driver
     * sends the same MADCTL values (dash_s3_display.cpp), so the transform
     * carries over unchanged:
     *
     *   Landscape (MADCTL 0xE8):  x = 479 - raw_y,  y = raw_x
     *   Portrait  (MADCTL 0x48):  x = raw_x,        y = raw_y
     *
     * Landscape's raw Y inverts against 480 rather than its own 320 width
     * because raw Y always runs along the panel's LONG edge whichever way
     * the display is rotated. That asymmetry is real, not a typo. If the
     * MADCTL values in the display driver change, re-derive these from
     * the Teensy's measurements rather than re-measuring from scratch. */
    if (orientation_ == DisplayOrientation::Portrait)
    {
        out.x = x_val_;
        out.y = y_val_;
        if (flipped_)
        {
            flip_touch_point(out, LCD_VERT, LCD_HORZ);
        }
    }
    else
    {
        out.x = static_cast<int16_t>((LCD_HORZ - 1) - y_val_);
        out.y = x_val_;
        if (flipped_)
        {
            flip_touch_point(out, LCD_HORZ, LCD_VERT);
        }
    }

    return pressed;
}
