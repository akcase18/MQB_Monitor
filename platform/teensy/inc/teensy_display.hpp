/**
 * @file teensy_display.hpp
 * @brief Teensy IDisplay implementation. Phase 1 stub — no
 * ILI948x_t4_mm wiring yet, see teensy_display.cpp.
 */
#pragma once

#include "platform.hpp"

class TeensyDisplay : public IDisplay
{
    public:
        static TeensyDisplay &instance();

        void begin() override;
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
};
