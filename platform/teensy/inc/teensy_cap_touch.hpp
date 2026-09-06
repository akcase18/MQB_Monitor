/**
 * @file teensy_cap_touch.hpp
 * @brief Teensy ITouch implementation for the FT6236 capacitive touch
 * controller over I2C. Kept alongside teensy_touch.hpp's resistive
 * XPT2046 implementation -- see teensy_platform.cpp for which is active.
 */
#pragma once

#include "platform.hpp"

class TeensyCapTouch : public ITouch
{
    public:
        static TeensyCapTouch &instance();

        void begin() override;
        void set_orientation(DisplayOrientation orientation) override;
        bool read(TouchPoint &out) override;

    private:
        TeensyCapTouch() = default;

        static bool read_reg(uint8_t reg, uint8_t &val);
        static void write_reg(uint8_t reg, uint8_t val);

        int16_t            x_val_       = 0;
        int16_t            y_val_       = 0;
        DisplayOrientation orientation_ = DisplayOrientation::Landscape;
};
