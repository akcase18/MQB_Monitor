/**
 * @file esp32_clock.hpp
 * @brief IClock over Arduino's millis().
 */
#pragma once

#include "platform.hpp"

class Esp32Clock : public IClock
{
    public:
        static Esp32Clock &instance();

        uint32_t millis() const override;

    private:
        Esp32Clock() = default;
};
