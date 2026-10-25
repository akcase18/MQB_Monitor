/**
 * @file dash_s3_res_touch.cpp
 * @brief Raw SPI XPT2046 touch driver, ported from
 * platform/teensy/src/teensy_touch.cpp (get_best_avg / rotation-remap
 * logic kept verbatim). Polling only; the controller's pen-interrupt line
 * is left unconnected.
 *
 * The bus is the S3's second general-purpose SPI host (HSPI) on its own
 * pins rather than the default `SPI` object, so nothing else that might
 * want the default bus later collides with a 1MHz touch poll. Any GPIO
 * works for it -- the S3 routes SPI through the GPIO matrix.
 */

#include "dash_s3_res_touch.hpp"

#include <Arduino.h>
#include <SPI.h>

#include "dash_s3_pins.hpp"

namespace
{
    constexpr uint16_t X_CMD       = 0x91;
    constexpr uint16_t Y_CMD       = 0xD1;
    constexpr uint16_t Z1_CMD      = 0xB1;
    constexpr uint16_t Z2_CMD      = 0xC1;
    constexpr int16_t  Z_THRESHOLD = 800;
    /* Contact area shrinks as a finger lifts off, which skews the X/Y
     * reading (often toward a screen edge) for the last poll or two before
     * pressure finally drops below Z_THRESHOLD -- that transient still
     * counts as "pressed" but its coordinates aren't trustworthy. Below
     * this stricter threshold, keep reporting the last confident position
     * instead of the fresh (possibly skewed) one. */
    constexpr int16_t  Z_CONFIDENT_THRESHOLD = 1000;
    constexpr uint32_t MIN_CYCLE_TIME        = 3;

    /* XPT2046 tops out at 2.5MHz on its own clock; 1MHz matches the
     * Teensy driver and is what the measured thresholds above were tuned
     * against. */
    constexpr uint32_t SPI_HZ = 1000000;

    SPIClass s_spi(HSPI);
    uint32_t s_last_poll_tick = 0;
} // namespace

DashS3ResTouch &DashS3ResTouch::instance()
{
    static DashS3ResTouch s_instance;
    return s_instance;
}

void DashS3ResTouch::begin()
{
    pinMode(DASH_S3_PIN_RTP_CS, OUTPUT);
    digitalWrite(DASH_S3_PIN_RTP_CS, HIGH);

    /* CS is driven by hand around each poll (the controller wants it held
     * low across the whole burst of conversions), so -1 keeps the library
     * from claiming a hardware CS pin. */
    s_spi.begin(DASH_S3_PIN_RTP_SCK,
                DASH_S3_PIN_RTP_MISO,
                DASH_S3_PIN_RTP_MOSI,
                -1);
}

void DashS3ResTouch::set_orientation(DisplayOrientation orientation,
                                     bool               flipped)
{
    orientation_ = orientation;
    flipped_     = flipped;
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

void DashS3ResTouch::poll()
{
    uint32_t curr_tick = millis();
    if ((curr_tick - s_last_poll_tick) < MIN_CYCLE_TIME)
    {
        return;
    }

    s_spi.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(DASH_S3_PIN_RTP_CS, LOW);

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

    digitalWrite(DASH_S3_PIN_RTP_CS, HIGH);
    s_spi.endTransaction();

    int16_t x = get_best_avg(x1, x2, x3);
    int16_t y = get_best_avg(y1, y2, y3);

    if (z < Z_THRESHOLD)
    {
        /* x_val_/y_val_ deliberately keep their last value here -- see the
         * comment on read() below for why reporting (0,0) on release is
         * wrong, not just cosmetically "less precise". */
        z_val_           = 0;
        was_pressed_     = false;
        s_last_poll_tick = curr_tick;
        return;
    }

    z_val_ = z;
    /* Always accept the first sample of a new press (no prior confident
     * position to fall back on) -- only hold the last position once a
     * press is already established and pressure drops into the
     * low-confidence zone. */
    if (!was_pressed_ || z > Z_CONFIDENT_THRESHOLD)
    {
        /* Both transforms are the Teensy driver's, measured on this panel
         * against the same MADCTL values dash_s3_display.cpp sends. If a
         * corner lands in the wrong place, this is where to swap or
         * invert -- and the fix belongs in teensy_touch.cpp too, since
         * the panel and rotation are shared. */
        if (orientation_ == DisplayOrientation::Portrait)
        {
            x_val_ = 4095 - x;
            y_val_ = 4095 - y;
        }
        else
        {
            x_val_ = 4095 - y;
            y_val_ = x;
        }
    }
    was_pressed_ = true;

    s_last_poll_tick = curr_tick;
}

bool DashS3ResTouch::read(TouchPoint &out)
{
    poll();

    bool pressed = z_val_ >= Z_THRESHOLD;
    out.pressed  = pressed;
    /* LVGL's indev core updates its internal "current point" from
     * whatever this call reports even when state is released -- a widget
     * being dragged (e.g. lv_slider) finalizes its value from that exact
     * point on the release event. Reporting (0,0) here would make LVGL
     * finish every single drag at the coordinate origin instead of
     * wherever the finger actually was, so the last valid touch position
     * is reported regardless of pressed state; `out.pressed` is what
     * actually communicates press/release to LVGL. */
    out.x = (x_val_ * width_) / 4095;
    out.y = (y_val_ * height_) / 4095;
    if (flipped_)
    {
        flip_touch_point(out, width_, height_);
    }

    return pressed;
}

int16_t DashS3ResTouch::get_single_axis(uint16_t cmd)
{
    return s_spi.transfer16(cmd) >> 3;
}

int16_t DashS3ResTouch::get_best_avg(int16_t x, int16_t y, int16_t z)
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
