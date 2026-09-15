/**
 * @file esp32s3_display.hpp
 * @brief IDisplay over an ST7789V2 panel on the ESP32-S3's SPI bus.
 *
 * A 320x172 landscape strip. Written directly against Arduino's SPIClass
 * rather than pulling in a display library: the driver is an init
 * sequence, a window-set and a pixel push, and the two things that make
 * *this* panel different from a generic ST7789 are both things a library
 * would hide behind a configuration flag that is easy to get wrong.
 *
 * ## The two panel-specific details, stated once
 *
 * **1. The GRAM offset.** An ST7789 has 240x320 of frame memory. A
 * 172-pixel-wide panel is wired to the *middle* of the 240 axis, so every
 * address has to be shifted by `(240 - 172) / 2 = 34`. In landscape the
 * 240 axis is the rows, so the offset lands on Y. Get it wrong and the
 * display shows a garbage stripe down one edge and everything shifted --
 * the single most common ST7789 bring-up bug, which is why it is a named
 * constant here and not a magic number inside set_window().
 *
 * **2. RGB565 byte order.** LVGL renders little-endian; the ST7789 reads
 * big-endian off the wire. flush() swaps in chunks on the way out rather
 * than asking LVGL to render the other way round, because IDisplay::flush
 * takes a `const uint16_t *` -- the buffer belongs to LVGL and this driver
 * does not get to scribble on it. Wrong byte order does not fail; it
 * produces a picture in believable but wrong colours, which is easy to
 * stare past.
 *
 * Neither is verifiable off hardware. The desktop simulator renders LVGL's
 * own output and knows nothing about either.
 */
#pragma once

#include "platform.hpp"

class Esp32s3Display : public IDisplay
{
    public:
        /** The panel's own dimensions, landscape. Not a build flag: this
         * is the part fitted, not a preference. */
        static constexpr uint16_t PANEL_LONG_SIDE  = 320;
        static constexpr uint16_t PANEL_SHORT_SIDE = 172;

        /** ST7789 frame memory, which is larger than this panel in one
         * axis -- see the file comment. */
        static constexpr uint16_t GRAM_SHORT_SIDE = 240;

        static constexpr uint16_t GRAM_OFFSET
            = (GRAM_SHORT_SIDE - PANEL_SHORT_SIDE) / 2; /* 34 */

        static Esp32s3Display &instance();

        void begin() override;
        void set_orientation(DisplayOrientation orientation) override;
        void get_resolution(uint16_t &width, uint16_t &height) const override;
        void flush(int32_t         x1,
                   int32_t         y1,
                   int32_t         x2,
                   int32_t         y2,
                   const uint16_t *pixels) override;
        void set_backlight(uint8_t pct) override;
        void set_backlight_off() override;

    private:
        Esp32s3Display() = default;

        /**
         * Sends one command and its parameters as a single chip-select
         * window.
         *
         * Taking the parameters here rather than in a separate call is
         * not tidiness: **CS must stay low from the command byte through
         * the last parameter.** Raising it in between ends the command as
         * far as the controller is concerned, and the parameters that
         * follow are interpreted as a fresh command. The failure is a
         * panel that shows nothing at all, with every byte visibly
         * correct on a logic analyser.
         */
        void write_command(uint8_t        cmd,
                           const uint8_t *params = nullptr,
                           size_t         len    = 0);

        void set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

        DisplayOrientation orientation_ = DisplayOrientation::Landscape;
        uint16_t           width_       = PANEL_LONG_SIDE;
        uint16_t           height_      = PANEL_SHORT_SIDE;
        uint16_t           x_offset_    = 0;
        uint16_t           y_offset_    = GRAM_OFFSET;
        bool               began_       = false;
};
