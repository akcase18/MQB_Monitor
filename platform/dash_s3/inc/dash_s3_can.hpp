/**
 * @file dash_s3_can.hpp
 * @brief ICanBus over the ESP32-S3's TWAI controller (ESP-IDF driver/twai).
 * Needs an external 3.3V CAN transceiver on the TX/RX pins.
 */
#pragma once

#include "platform.hpp"

class DashS3CanBus : public ICanBus
{
    public:
        static DashS3CanBus &instance();

        bool begin(uint32_t tx_id, uint32_t rx_id, uint32_t baud) override;
        bool send(const CanFrame &frame) override;
        bool receive(CanFrame &out) override;

    private:
        DashS3CanBus() = default;

        bool started_ = false;
};
