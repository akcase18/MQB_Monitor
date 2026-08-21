/**
 * @file teensy_touch.cpp
 * @brief Raw SPI XPT2046-style touch driver, ported from the old
 * Touchscreen class (get_best_avg/rotation-remap logic kept verbatim).
 * Interrupt-driven wake was defined but never actually used by the old
 * main.cpp (always constructed cs-only), so this is a straight polling
 * driver.
 */

#include "teensy_touch.hpp"

#include <Arduino.h>
#include <SPI.h>

namespace
{
    /* CS1 */
    constexpr uint8_t CS_PIN = 0;

    constexpr uint16_t X_CMD       = 0x91;
    constexpr uint16_t Y_CMD       = 0xD1;
    constexpr uint16_t Z1_CMD      = 0xB1;
    constexpr uint16_t Z2_CMD      = 0xC1;
    constexpr int16_t  Z_THRESHOLD = 800;
    /* Contact area shrinks as a finger lifts off, which skews the X/Y
     * reading (often toward a screen edge) for the last poll or two before
     * pressure finally drops below Z_THRESHOLD — that transient still
     * counts as "pressed" but its coordinates aren't trustworthy. Below
     * this stricter threshold, keep reporting the last confident position
     * instead of the fresh (possibly skewed) one. */
    constexpr int16_t  Z_CONFIDENT_THRESHOLD = 1000;
    constexpr uint32_t MIN_CYCLE_TIME        = 3;

    uint32_t s_last_poll_tick = 0;
} // namespace

TeensyTouch &TeensyTouch::instance()
{
    static TeensyTouch s_instance;
    return s_instance;
}

void TeensyTouch::begin()
{
    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);
    SPI1.begin();
}

void TeensyTouch::set_orientation(DisplayOrientation orientation)
{
    orientation_ = orientation;
    if (orientation == DisplayOrientation::Portrait)
    {
        width_  = 320;
        height_ = 480;
    }
    else
    {
        width_  = 480;
        height_ = 320;
    }
}

void TeensyTouch::poll()
{
    uint32_t curr_tick = millis();
    if ((curr_tick - s_last_poll_tick) < MIN_CYCLE_TIME)
    {
        return;
    }

    SPI1.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CS_PIN, LOW);

    int16_t z1 = get_single_axis(Z1_CMD);
    z1         = get_single_axis(Z1_CMD);
    int16_t z2 = get_single_axis(Z2_CMD);
    z2         = get_single_axis(Z2_CMD);

    int16_t z  = z1 + 4095 - z2;
    int16_t x1 = 0;
    int16_t x2 = 0;
    int16_t x3 = 0;
    int16_t y1 = 0;
    int16_t y2 = 0;
    int16_t y3 = 0;

    if (z > Z_THRESHOLD)
    {
        get_single_axis(X_CMD); /* initial query to reduce noise */
        x1 = get_single_axis(X_CMD);
        y1 = get_single_axis(Y_CMD);
        x2 = get_single_axis(X_CMD);
        y2 = get_single_axis(Y_CMD);
        x3 = get_single_axis(X_CMD);
        y3 = get_single_axis(Y_CMD);
    }

    digitalWrite(CS_PIN, HIGH);
    SPI1.endTransaction();

    int16_t x = get_best_avg(x1, x2, x3);
    int16_t y = get_best_avg(y1, y2, y3);

    if (z < Z_THRESHOLD)
    {
        /* x_val_/y_val_ deliberately keep their last value here — see the
         * comment on read() below for why reporting (0,0) on release is
         * wrong, not just cosmetically "less precise". */
        z_val_           = 0;
        was_pressed_     = false;
        s_last_poll_tick = curr_tick;
        return;
    }

    z_val_ = z;
    /* Always accept the first sample of a new press (no prior confident
     * position to fall back on) — only hold the last position once a
     * press is already established and pressure drops into the
     * low-confidence zone. */
    if (!was_pressed_ || z > Z_CONFIDENT_THRESHOLD)
    {
        if (orientation_ == DisplayOrientation::Portrait)
        {
            /* Confirmed on real hardware: touching the bottom-right
             * corner registered as top-right, bottom-left as top-left,
             * top-left as bottom-left -- X was already correct, Y came
             * out mirrored. This also lines up with teensy_display.cpp's
             * own Portrait fix (setRotation() corrected from the guessed
             * 0 to the confirmed-correct 2, a 180-degree difference) --
             * x_val_ is unchanged from the original guess since it was
             * already right, y_val_ is now inverted to match. */
            x_val_ = 4095 - x;
            y_val_ = 4095 - y;
        }
        else
        {
            /* rotation 2, matching the old driver's unchanged default,
             * then flipped 180 -- x_val_/y_val_ each negated relative to
             * their 0-4095 range -- to track the display's
             * setRotation(3) (see teensy_display.cpp). */
            x_val_ = 4095 - y;
            y_val_ = x;
        }
    }
    was_pressed_ = true;

    s_last_poll_tick = curr_tick;
}

bool TeensyTouch::read(TouchPoint &out)
{
    poll();

    bool pressed = z_val_ >= Z_THRESHOLD;
    out.pressed  = pressed;
    /* LVGL's indev core updates its internal "current point" from
     * whatever this call reports even when state is released — a widget
     * being dragged (e.g. lv_slider) finalizes its value from that exact
     * point on the release event. Reporting (0,0) here would make LVGL
     * finish every single drag at the coordinate origin instead of
     * wherever the finger actually was, so the last valid touch position
     * is reported regardless of pressed state; `out.pressed` is what
     * actually communicates press/release to LVGL. */
    out.x = (x_val_ * width_) / 4095;
    out.y = (y_val_ * height_) / 4095;

    return pressed;
}

int16_t TeensyTouch::get_single_axis(uint16_t cmd)
{
    return SPI1.transfer16(cmd) >> 3;
}

int16_t TeensyTouch::get_best_avg(int16_t x, int16_t y, int16_t z)
{
    int16_t diff_a = (x > y) ? (x - y) : (y - x);
    int16_t diff_b = (x > z) ? (x - z) : (z - x);
    int16_t diff_c = (y > z) ? (y - z) : (z - y);

    if (diff_a <= diff_b && diff_a <= diff_c)
    {
        return (x + y) >> 1;
    }
    if (diff_b <= diff_a && diff_b <= diff_c)
    {
        return (x + z) >> 1;
    }
    return (y + z) >> 1;
}
