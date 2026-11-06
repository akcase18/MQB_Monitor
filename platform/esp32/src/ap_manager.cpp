#include "ap_manager.hpp"

#include <WiFi.h>
#include <esp_wifi.h>

namespace esp32
{
    namespace
    {
        /**
         * How long a joined phone can go silent before the AP drops it
         * (ESP-IDF's default is 300 s, its minimum 10). A phone that
         * walks out of range only leaves the station count this way, and
         * the panel goes back to the QR code when the last one does. A
         * phone in range, even locked, sends well inside this.
         */
        constexpr uint16_t STATION_INACTIVE_S = 30;
    } // namespace

    void ApManager::enable(const char *ssid, const char *password)
    {
        WiFi.mode(WIFI_AP);

        /**
         * An empty password means "open AP" (see RemoteApController's file
         * doc comment on the Teensy side) — pass nullptr explicitly rather
         * than relying on WiFi.softAP()'s empty-string handling.
         */
        bool has_password = password != nullptr && password[0] != '\0';
        WiFi.softAP(ssid, has_password ? password : nullptr);
        esp_wifi_set_inactive_time(WIFI_IF_AP, STATION_INACTIVE_S);

        enabled_ = true;
    }

    void ApManager::disable()
    {
        WiFi.softAPdisconnect(true);
        enabled_ = false;
    }

    bool ApManager::is_enabled() const
    {
        return enabled_;
    }

    uint8_t ApManager::station_count() const
    {
        return enabled_ ? WiFi.softAPgetStationNum() : 0;
    }

    void ApManager::get_ip(uint8_t ip_out[4]) const
    {
        IPAddress ip = WiFi.softAPIP();
        ip_out[0]    = ip[0];
        ip_out[1]    = ip[1];
        ip_out[2]    = ip[2];
        ip_out[3]    = ip[3];
    }

} // namespace esp32
