/**
 * @file dash_s3_touch.hpp
 * @brief ITouch for the dash's FT6236 capacitive touch controller over
 * I2C. Based off Teensy's capacitive touch library.
 */
#pragma once

#include "platform.hpp"

class DashS3Touch : public ITouch
{
    public:
        static DashS3Touch &instance();

        void begin() override;
        void set_orientation(DisplayOrientation orientation,
                             bool               flipped) override;
        bool read(TouchPoint &out) override;

    private:
        DashS3Touch() = default;

        static bool read_reg(uint8_t reg, uint8_t &val);
        static void write_reg(uint8_t reg, uint8_t val);

        bool               present_     = false;
        int16_t            x_val_       = 0;
        int16_t            y_val_       = 0;
        DisplayOrientation orientation_ = DisplayOrientation::Landscape;
        bool               flipped_     = false;
};
