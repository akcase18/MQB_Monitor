/**
 * @file esp32s3_clock.hpp
 * @brief IClock over Arduino's millis().
 */
#pragma once

#include "platform.hpp"

class Esp32s3Clock : public IClock
{
    public:
        static Esp32s3Clock &instance();

        uint32_t millis() const override;

    private:
        Esp32s3Clock() = default;
};
