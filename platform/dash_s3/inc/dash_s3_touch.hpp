/**
 * @file dash_s3_touch.hpp
 * @brief ITouch for the dash's FT6236 capacitive touch controller over
 * I2C. Same controller and same panel as the Teensy's teensy_cap_touch --
 * only the bus pins and the I2C library's timeout handling differ.
 */
#pragma once

#include "platform.hpp"

class DashS3Touch : public ITouch
{
    public:
        static DashS3Touch &instance();

        void begin() override;
        void set_orientation(DisplayOrientation orientation) override;
        bool read(TouchPoint &out) override;

    private:
        DashS3Touch() = default;

        static bool read_reg(uint8_t reg, uint8_t &val);
        static void write_reg(uint8_t reg, uint8_t val);

        bool               present_     = false;
        int16_t            x_val_       = 0;
        int16_t            y_val_       = 0;
        DisplayOrientation orientation_ = DisplayOrientation::Landscape;
};
