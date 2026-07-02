/**
 * @file teensy_touch.hpp
 * @brief Teensy ITouch implementation. Phase 1 stub — no raw SPI
 * touch controller wiring yet, see teensy_touch.cpp.
 */
#pragma once

#include "platform.hpp"

class TeensyTouch : public ITouch
{
    public:
        static TeensyTouch &instance();

        void begin() override;
        bool read(TouchPoint &out) override;

    private:
        TeensyTouch() = default;
};
