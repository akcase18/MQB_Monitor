#include "ap_manager.hpp"

#include <WiFi.h>

namespace esp32
{
    void ApManager::enable(const char *ssid, const char *password)
    {
        WiFi.mode(WIFI_AP);

        /**
         * An empty password means "open AP" (see RemoteApController's file
         * doc comment on the Teensy side) — pass nullptr explicitly rather
         * than relying on WiFi.softAP()'s empty-string handling. */
        bool has_password = password != nullptr && password[0] != '\0';
        WiFi.softAP(ssid, has_password ? password : nullptr);

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

    void ApManager::get_ip(uint8_t ip_out[4]) const
    {
        IPAddress ip = WiFi.softAPIP();
        ip_out[0]    = ip[0];
        ip_out[1]    = ip[1];
        ip_out[2]    = ip[2];
        ip_out[3]    = ip[3];
    }

} // namespace esp32
