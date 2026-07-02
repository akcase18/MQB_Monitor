/**
 * @file teensy_clock.hpp
 * @brief Teensy IClock implementation. Phase 1 stub — wired to
 * Arduino millis() in Phase 2.
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
