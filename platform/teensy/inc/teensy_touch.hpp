/**
 * @file teensy_touch.hpp
 * @brief Teensy ITouch implementation. Raw SPI XPT2046-style touch
 * controller driver, ported from the old Touchscreen class.
 */
#pragma once

#include "platform.hpp"

class TeensyTouch : public ITouch
{
    public:
        static TeensyTouch &instance();

        void begin() override;
        void set_orientation(DisplayOrientation orientation,
                             bool               flipped) override;
        bool read(TouchPoint &out) override;

    private:
        TeensyTouch() = default;

        int16_t get_single_axis(uint16_t cmd);
        int16_t get_best_avg(int16_t x, int16_t y, int16_t z);
        void    poll();

        int16_t            x_val_       = 0;
        int16_t            y_val_       = 0;
        int16_t            z_val_       = 0;
        bool               was_pressed_ = false;
        DisplayOrientation orientation_ = DisplayOrientation::Landscape;
        bool               flipped_     = false;
        uint16_t           width_       = 480;
        uint16_t           height_      = 320;
};
