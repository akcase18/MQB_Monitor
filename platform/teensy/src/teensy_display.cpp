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

    /* ILI948x_t4_mm::setRotation() rotation values: 1/3 are landscape
     * (480x320, mirror pair), 0/2 are portrait (320x480, mirror pair).
     * Rotation 3 is the confirmed-correct landscape mounting (an earlier
     * 180-degree-flip fix). Rotation 0 for Portrait was an unverified
     * guess -- confirmed on real hardware to be 180 degrees off, so 2 (the
     * other half of the portrait mirror pair) is the corrected value. */
    constexpr uint8_t ROTATION_LANDSCAPE = 3;
    constexpr uint8_t ROTATION_PORTRAIT  = 2;

    constexpr uint8_t PWM_PIN = 3;

    ILI948x_t4_mm s_lcd(LCD_DC, LCD_CS, LCD_RST);
} // namespace

TeensyDisplay &TeensyDisplay::instance()
{
    static TeensyDisplay s_instance;
    return s_instance;
}

/* FlexIO bus speed in MHz. The driver turns this into a shift-clock
 * divider off its 240MHz FlexIO clock (480MHz source / 2, see
 * ILI948x_t4_mm::FlexIO_Init): 40 -> _buad_div 6 -> 240/6 = 40MHz. The
 * bus is 8 bits wide and setBitDepth(16) makes a pixel two beats, so
 * this is also the byte rate -- a full 480x320 frame is 307,200 bytes,
 * i.e. 7.7ms of DMA at 40MHz against 15.4ms at 20 and 25.6ms at 12.
 *
 * ONLY these values work: 2, 4, 8, 12, 20, 24, 30, 40. ILI948x_t4_mm::
 * begin() switches on the argument and its default case silently falls
 * back to 12MHz -- so a plausible-looking 16 or 25 here would quietly
 * run the bus slower than a 12 would, with nothing to indicate it.
 *
 * 40 is deliberately past what the panel is specified for: ILI9488's
 * 8080 parallel write cycle is typically quoted at ~50ns minimum, i.e.
 * about 20MHz, and 40MHz halves that to 25ns. It is being tried anyway
 * to find where this particular board and ribbon actually fall over.
 * The failure mode is corrupted or torn pixels rather than a clean
 * error, and it may well be temperature- and cable-dependent rather
 * than reproducible -- so judge it on a warm panel over a few minutes,
 * not on one boot. Step back down (30, 24, 20) if anything looks wrong;
 * 20 is the fastest in-spec value. */
constexpr uint8_t LCD_BUS_MHZ = 40;

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

    /* Argument is the FlexIO bus speed in MHz, which the driver turns
     * into a shift-clock divider off its 240MHz FlexIO clock (480MHz
     * source / 2, see ILI948x_t4_mm::FlexIO_Init) -- begin(20) selects
     * _buad_div = 12, i.e. 240/12 = 20MHz. The bus is 8 bits wide and
     * setBitDepth(16) makes a pixel two beats, so this is also the byte
     * rate: 20MB/s, against 12MB/s at the driver's begin(12) default.
     * A full 480x320 frame is 307,200 bytes, so worst case that is
     * 15.4ms of DMA rather than 25.6ms.
     *
     * Raise further (the driver's table goes to 40MHz) only with the
     * panel in hand -- this is an unterminated parallel bus over ribbon,
     * and the failure mode for too-fast is corrupted pixels, not a clean
     * error. */
    s_lcd.begin(20);
    s_lcd.setBitDepth(16);
}

void TeensyDisplay::set_orientation(DisplayOrientation orientation)
{
    if (orientation == DisplayOrientation::Portrait)
    {
        s_lcd.setRotation(ROTATION_PORTRAIT);
        width_  = LCD_VERT;
        height_ = LCD_HORZ;
    }
    else
    {
        s_lcd.setRotation(ROTATION_LANDSCAPE);
        width_  = LCD_HORZ;
        height_ = LCD_VERT;
    }
}

void TeensyDisplay::get_resolution(uint16_t &width, uint16_t &height) const
{
    width  = width_;
    height = height_;
}

void TeensyDisplay::flush(
    int32_t x1, int32_t y1, int32_t x2, int32_t y2, const uint16_t *pixels)
{
    /* pushPixels16bitDMA blocks internally until any *prior* transfer has
     * completed, then starts this one and returns -- so returning from
     * here means the flush is issued, not that its pixels have landed.
     * For every flush but a frame's last that's invisible: the next
     * flush's own entry-wait covers it. The last one is genuinely still
     * in flight, which is why src/main.cpp waits for the display to go
     * quiet before lighting the backlight rather than trusting
     * lv_display_flush_is_last(). */
    s_lcd.pushPixels16bitDMA(pixels, x1, y1, x2, y2);
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

void TeensyDisplay::set_backlight_off()
{
    /* Deliberately not set_backlight(0), which floors at
     * MIN_BACKLIGHT_PCT and would leave the panel dimly lit -- still
     * bright enough to show the transition this is meant to hide. Drives
     * the pin the same way begin() does, for the same reason. */
    analogWrite(PWM_PIN, 0);
}
