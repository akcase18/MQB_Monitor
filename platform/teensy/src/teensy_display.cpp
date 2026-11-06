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
    /**
     * D0
     */
    constexpr int8_t LCD_CS = 4;
    /**
     * D1
     */
    constexpr int8_t LCD_DC = 5;
    /**
     * G9
     */
    constexpr int8_t LCD_RST = 32;

    constexpr uint16_t LCD_HORZ = 480;
    constexpr uint16_t LCD_VERT = 320;

    /**
     * ILI948x_t4_mm::setRotation() rotation values: 1/3 are landscape
     * (480x320, mirror pair), 0/2 are portrait (320x480, mirror pair).
     * Rotation 3 is the confirmed-correct landscape mounting.
     *
     * Portrait has been 0 -> 2 -> 0 across board revisions: 0 was an
     * unverified guess, 2 corrected it on the old board, and the v3 PCB
     * mounts the panel the other way up again, so 0 is correct once more.
     * MADCTL[0] is MX and MADCTL[2] is MY -- a horizontal mirror and a
     * vertical mirror of the same image, which differ from each other by
     * exactly 180 degrees, so these two are the flip pair.
     *
     * Changing this REQUIRES a matching change in teensy_cap_touch.cpp:
     * flipping the panel 180 degrees inverts both screen axes, so the
     * Portrait touch transform inverts with it. The two are only correct
     * together.
     */
    constexpr uint8_t ROTATION_LANDSCAPE = 3;
    constexpr uint8_t ROTATION_PORTRAIT  = 0;

    /**
     * Each mounting's 180-degree partner -- the same orientation with the
     * panel the other way up (IDisplay::set_orientation's `flipped`).
     * MADCTL[1] (MV) vs MADCTL[3] (MX|MY|MV) and MADCTL[0] (MX) vs
     * MADCTL[2] (MY) each differ by exactly MX^MY, which is a 180-degree
     * turn. Touch follows via flip_touch_point(), not a new transform.
     */
    constexpr uint8_t ROTATION_LANDSCAPE_FLIPPED = 1;
    constexpr uint8_t ROTATION_PORTRAIT_FLIPPED  = 2;

    /**
     * Backlight PWM -> the CTRL pin (pin 4 of the SC-70-5) of the
     * TPS61169DCKR WLED boost driver, replacing the v2 board's
     * PWM-into-a-breakout-board arrangement.
     *
     * The TPS61169 does ANALOG dimming, not chopped-LED dimming: it
     * chops its internal 204mV reference at the CTRL duty cycle, runs
     * that through an internal low-pass filter, and regulates FB to
     * 204mV * duty (SNVSA40B section 6.3.5). LED current therefore comes
     * out DC and proportional to duty, which is why nothing here needs
     * to worry about the backlight beating against the panel refresh.
     */
    constexpr uint8_t PWM_PIN = 3;

    /**
     * CTRL carrier frequency. The datasheet calls for 5kHz-100kHz "for
     * optimum performance": below 5kHz the pulse train falls outside the
     * internal low-pass filter's range, so the FB regulation voltage
     * ripples, the output ripples with it, and the part can generate
     * audible noise (SNVSA40B section 6.3.5).
     *
     * That requirement is the entire reason begin() sets this. Teensy
     * 4's analogWrite() default is 4482Hz -- F_BUS_ACTUAL 150MHz /
     * (VAL1 33464 + 1), see cores/teensy4/pwm.c -- which is UNDER the
     * 5kHz minimum. Leaving the default alone is a real out-of-spec
     * condition, not a rounding error.
     *
     * 20kHz sits comfortably inside the window and above audible, in
     * case anything downstream of the driver is piezoelectric. At 150MHz
     * that is 7500 timer counts per period, so the 8-bit analogWrite
     * range still resolves ~29 counts per step -- running fast costs no
     * dimming resolution.
     *
     * Pin 3 shares FlexPWM4_2 with pin 2, so this is really the
     * submodule's frequency. Pin 2 is the SD card's chip select and is
     * driven as plain GPIO (teensy_sd_card.cpp), so it never sees the
     * timer.
     */
    constexpr uint32_t PWM_FREQ_HZ = 20000;

    ILI948x_t4_mm s_lcd(LCD_DC, LCD_CS, LCD_RST);
} // namespace

TeensyDisplay &TeensyDisplay::instance()
{
    static TeensyDisplay s_instance;
    return s_instance;
}

/**
 * FlexIO bus speed in MHz. The driver turns this into a shift-clock
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
 * 20 is the fastest in-spec value.
 */
constexpr uint8_t LCD_BUS_MHZ = 40;

void TeensyDisplay::begin()
{
    /**
     * Park CTRL low before touching the panel at all. pinMode() defaults
     * to INPUT at boot, and while the TPS61169's 300k internal pull-down
     * (SNVSA40B, R_PD) does hold a floating CTRL down on its own, that is
     * a property of the driver rather than something this code should be
     * leaning on -- driving it actively costs one register write.
     *
     * Held low, CTRL is below the 1.2V rising-edge threshold so the boost
     * converter never enables, and past 2.5ms low (t_SD) the part is in
     * full shutdown. The backlight is genuinely off, not merely dim.
     *
     * Stays low after init too -- the panel's GRAM is still stale from
     * power-on until the first real frame is flushed, so it is on the
     * caller (see main.cpp's display_flush_cb) to light the backlight
     * once that has actually happened.
     */
    pinMode(PWM_PIN, OUTPUT);
    digitalWrite(PWM_PIN, LOW);

    /**
     * Must precede the first analogWrite(): the core documents the result
     * as unpredictable if the pin is already in PWM mode, and the pin is
     * still plain GPIO at this point.
     */
    analogWriteFrequency(PWM_PIN, static_cast<float>(PWM_FREQ_HZ));

    /**
     * Argument is the FlexIO bus speed in MHz, which the driver turns
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
     * error.
     */
    s_lcd.begin(LCD_BUS_MHZ);
    s_lcd.setBitDepth(16);
}

void TeensyDisplay::set_orientation(DisplayOrientation orientation,
                                    bool               flipped)
{
    /**
     * setRotation() waits out any in-flight DMA before sending MADCTL,
     * so a live flip can't interleave with a frame still going out.
     */
    if (orientation == DisplayOrientation::Portrait)
    {
        s_lcd.setRotation(flipped ? ROTATION_PORTRAIT_FLIPPED
                                  : ROTATION_PORTRAIT);
        width_  = LCD_VERT;
        height_ = LCD_HORZ;
    }
    else
    {
        s_lcd.setRotation(flipped ? ROTATION_LANDSCAPE_FLIPPED
                                  : ROTATION_LANDSCAPE);
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
    /**
     * pushPixels16bitDMA blocks internally until any *prior* transfer has
     * completed, then starts this one and returns -- so returning from
     * here means the flush is issued, not that its pixels have landed.
     * For every flush but a frame's last that's invisible: the next
     * flush's own entry-wait covers it. The last one is genuinely still
     * in flight, which is why src/main.cpp waits for the display to go
     * quiet before lighting the backlight rather than trusting
     * lv_display_flush_is_last().
     */
    s_lcd.pushPixels16bitDMA(pixels, x1, y1, x2, y2);
}

void TeensyDisplay::set_backlight(uint8_t pct)
{
    if (pct > 100)
    {
        pct = 100;
    }

    /**
     * The user-facing 0-100% maps onto MIN_BACKLIGHT_PCT-100% duty, so
     * "0%" is the dimmest the panel goes rather than off -- the
     * distinction IDisplay::set_backlight_off() exists to draw.
     *
     * On this driver that floor is structural, not cosmetic: duty 0 holds
     * CTRL low, and past t_SD (2.5ms) that IS the shutdown condition, so
     * a floor of 0 would collapse set_backlight(0) and
     * set_backlight_off() into the same call.
     *
     * 15% was calibrated against the v2 board, where anything below it
     * read as fully black; it does not carry over. The TPS61169
     * regulates LED current linearly with duty (I_LED = 204mV * duty /
     * R_SET), so low duty is genuinely dim here rather than a cliff.
     * 5% is picked as the smallest duty that still leaves the FB
     * reference (~10mV) clear of the error the 2.5uA FB bias current
     * contributes across R_SET -- the datasheet only specifies V_REF
     * accuracy at duty = 100%, so the bottom of the range is unspecified
     * territory and this is a margin call, not a spec. Re-calibrate on
     * hardware: raise it if the low end flickers or drops to black early,
     * lower it if the dimmest setting is still too bright at night.
     */
    constexpr uint32_t MIN_BACKLIGHT_PCT = 5;
    uint32_t           effective_pct
        = MIN_BACKLIGHT_PCT
          + (static_cast<uint32_t>(pct) * (100 - MIN_BACKLIGHT_PCT)) / 100;

    analogWrite(PWM_PIN, (effective_pct * 255) / 100);
}

void TeensyDisplay::set_backlight_off()
{
    /**
     * Deliberately not set_backlight(0), which floors at
     * MIN_BACKLIGHT_PCT and would leave the panel dimly lit -- still
     * bright enough to show the transition this is meant to hide.
     *
     * Duty 0 holds CTRL low continuously, which drops the TPS61169 below
     * its 1.2V enable threshold immediately and into shutdown after
     * t_SD (2.5ms), so this is a hard off. Coming back is just another
     * analogWrite() -- CTRL rising above 1.2V re-enables the converter
     * and re-runs its soft-start.
     */
    analogWrite(PWM_PIN, 0);
}
