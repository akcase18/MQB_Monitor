/**
 * @file dash_s3_res_touch.hpp
 * @brief ITouch for an XPT2046 resistive touch controller over SPI. Kept
 * alongside dash_s3_touch.hpp's capacitive FT6236 driver; see
 * dash_s3_platform.cpp for which is active.
 */
#pragma once

#include "platform.hpp"

class DashS3ResTouch : public ITouch
{
    public:
        static DashS3ResTouch &instance();

        void begin() override;
        void set_orientation(DisplayOrientation orientation,
                             bool               flipped) override;
        bool read(TouchPoint &out) override;

    private:
        DashS3ResTouch() = default;

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
