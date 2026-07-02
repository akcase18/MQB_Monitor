/**
 * @file teensy_clock.hpp
 * @brief Teensy IClock implementation, wired to Arduino millis().
 */
#pragma once

#include "platform.hpp"

class TeensyClock : public IClock
{
    public:
        static TeensyClock &instance();

        uint32_t millis() const override;

    private:
        TeensyClock() = default;
};
