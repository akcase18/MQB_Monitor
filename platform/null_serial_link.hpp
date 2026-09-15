/**
 * @file null_serial_link.hpp
 * @brief ISerialLink for a board with no companion processor to talk to.
 *
 * The CAN product reaches the outside world through an ESP32-C3 on a UART
 * (see remote/protocol). A product whose Wi-Fi is on-die has no such wire
 * and no second processor, so this is what fills Platform's reference.
 *
 * Reads return nothing and writes report zero bytes sent -- deliberately
 * not `len`, which would tell a caller its frame went out when nothing
 * happened.
 *
 * Header-only and board-agnostic, like the other nulls here.
 */
#pragma once

#include "platform.hpp"

class NullSerialLink : public ISerialLink
{
    public:
        static NullSerialLink &instance()
        {
            static NullSerialLink s_instance;
            return s_instance;
        }

        void begin(uint32_t baud) override
        {
            (void)baud;
        }

        size_t read(uint8_t *buf, size_t len) override
        {
            (void)buf;
            (void)len;
            return 0;
        }

        size_t write(const uint8_t *buf, size_t len) override
        {
            (void)buf;
            (void)len;
            return 0;
        }
};
