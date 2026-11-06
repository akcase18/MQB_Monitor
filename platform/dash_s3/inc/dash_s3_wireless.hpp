/**
 * @file dash_s3_wireless.hpp
 * @brief The S3's own Wi-Fi standing in for the ESP32-C3 relay. The C3's
 * whole relay (esp32::RelayApp: SoftAP lifecycle, the phone-facing config
 * form) runs in-process here, on its own task, and this class is the
 * ISerialLink main.cpp hands to remote::server -- the dash end of an
 * in-memory pipe whose other end feeds the relay. Neither side changed:
 * remote::server still writes CMD_AP_ENABLE frames and waits for
 * EVT_AP_READY; RelayApp still decodes them and stands up the AP. Only
 * the wire between them is a pair of FreeRTOS stream buffers rather than
 * a UART, and the C3 board is gone.
 */
#pragma once

#include "platform.hpp"

class DashS3Wireless : public ISerialLink
{
    public:
        static DashS3Wireless &instance();

        /**
         * Creates the pipe and starts the relay task. `baud` is
         * meaningless here and ignored.
         */
        void   begin(uint32_t baud) override;
        size_t read(uint8_t *buf, size_t len) override;
        size_t write(const uint8_t *buf, size_t len) override;

        /**
         * Whether the relay task has got through start-up -- Wi-Fi
         * initialised, the config and update pages registered -- and is
         * still coming round. A new firmware isn't kept without it
         * (platform_update_path_ready()): the next update arrives through
         * it.
         */
        bool relay_running() const;

    private:
        DashS3Wireless() = default;
};
