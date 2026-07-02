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

constexpr uint16_t X_CMD           = 0x91;
constexpr uint16_t Y_CMD           = 0xD1;
constexpr uint16_t Z1_CMD          = 0xB1;
constexpr uint16_t Z2_CMD          = 0xC1;
constexpr int16_t  Z_THRESHOLD     = 800;
constexpr uint32_t MIN_CYCLE_TIME  = 3;

constexpr uint16_t LCD_HORZ = 480;
constexpr uint16_t LCD_VERT = 320;

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
        z_val_ = 0;
        x_val_ = 0;
        y_val_ = 0;
        s_last_poll_tick = curr_tick;
        return;
    }

    z_val_ = z;
    /* rotation 2, matching the old driver's unchanged default */
    x_val_ = y;
    y_val_ = 4095 - x;

    s_last_poll_tick = curr_tick;
}

bool TeensyTouch::read(TouchPoint &out)
{
    poll();

    bool pressed = z_val_ >= Z_THRESHOLD;
    out.pressed  = pressed;
    if (pressed)
    {
        out.x = (x_val_ * LCD_HORZ) / 4095;
        out.y = (y_val_ * LCD_VERT) / 4095;
    }
    else
    {
        out.x = 0;
        out.y = 0;
    }

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
