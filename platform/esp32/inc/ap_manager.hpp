/**
 * @file ap_manager.hpp
 * @brief Thin wrapper around WiFi.h's SoftAP lifecycle. WiFi.h itself is the
 * real source of truth (WiFi.softAPIP() etc.) — this only tracks whether
 * enable()/disable() has been called, for the status page in main.cpp.
 */
#pragma once

#include <stdint.h>

namespace esp32
{
    class ApManager
    {
        public:
            void enable(const char *ssid, const char *password);
            void disable();
            bool is_enabled() const;

            /**
             * Fills `ip_out[4]` with the SoftAP's IPv4 address, matching
             * remote::protocol::ApReadyWire.ip's layout. Only meaningful
             * once is_enabled() is true — WiFi.softAP() is synchronous on
             * the Arduino-ESP32 core, so this is safe to call immediately
             * after enable() returns.
             */
            void get_ip(uint8_t ip_out[4]) const;

            /**
             * Phones joined to the AP; 0 while it's down.
             */
            uint8_t station_count() const;

        private:
            bool enabled_ = false;
    };

} // namespace esp32
