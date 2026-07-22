/**
 * @file teensy_serial_link.hpp
 * @brief Teensy ISerialLink implementation, backed by a spare hardware
 * Serial (Serial4). Carries the remote/protocol framed byte stream to/from
 * the companion ESP32-C3.
 */
#pragma once

#include "platform.hpp"

class TeensySerialLink : public ISerialLink
{
    public:
        static TeensySerialLink &instance();

        void   begin(uint32_t baud) override;
        size_t read(uint8_t *buf, size_t len) override;
        size_t write(const uint8_t *buf, size_t len) override;

    private:
        TeensySerialLink() = default;
};
