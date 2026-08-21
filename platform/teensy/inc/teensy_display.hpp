/**
 * @file teensy_display.hpp
 * @brief Teensy IDisplay implementation, wrapping ILI948x_t4_mm.
 */
#pragma once

#include "platform.hpp"

class TeensyDisplay : public IDisplay
{
    public:
        static TeensyDisplay &instance();

        void begin() override;
        void set_orientation(DisplayOrientation orientation) override;
        void get_resolution(uint16_t &width, uint16_t &height) const override;
        void flush(int32_t         x1,
                   int32_t         y1,
                   int32_t         x2,
                   int32_t         y2,
                   const uint16_t *pixels) override;
        bool flush_ready() const override;
        void set_backlight(uint8_t pct) override;

    private:
        TeensyDisplay() = default;

        uint16_t width_  = 480;
        uint16_t height_ = 320;
};
