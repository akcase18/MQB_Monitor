/**
 * @file remote_ap_controller.hpp
 * @brief Drives the ESP32-C3's Wi-Fi access point lifecycle from the
 * Teensy side: Idle -> Activating -> Active -> Deactivating -> Idle.
 * The AP is deliberately open (no password) — reaching it already requires
 * physical proximity to read the SSID off this screen, and the config it
 * exposes isn't sensitive, so a password would only add friction. An idle
 * timeout auto-deactivates; a stuck Activating/Deactivating state falls
 * back to Idle on its own timeout rather than wedging forever if the C3
 * never replies.
 */
#pragma once

#include <stdint.h>

#include "platform.hpp"
#include "remote_config_link.hpp"

namespace remote::server
{
    enum class ApState : uint8_t
    {
        Idle,
        Activating,
        Active,
        Deactivating
    };

    constexpr uint32_t AP_ACTIVATE_TIMEOUT_MS
        = 10 * 1000; /* EVT_AP_READY never arrives */
    constexpr uint32_t AP_IDLE_TIMEOUT_MS
        = 5 * 60 * 1000; /* auto-disable after inactivity */
    constexpr uint32_t AP_IDLE_CHECK_PERIOD_MS = 1000;
    constexpr uint32_t AP_DISABLE_GRACE_MS
        = 3 * 1000; /* EVT_AP_DISABLED fallback */

    class RemoteApController
    {
        public:
            RemoteApController(RemoteConfigLink &link, IClock &clock);

            /** No-op unless state()==Idle. Sends CMD_AP_ENABLE. */
            void activate();

            /** No-op unless state()==Active. Sends CMD_AP_DISABLE. */
            void deactivate();

            /** Feed from RemoteConfigLink::poll()'s EventCallback. */
            void on_event(protocol::MsgType type,
                          const uint8_t    *payload,
                          uint16_t          len,
                          uint32_t          now_ms);

            /** Call periodically (e.g. from a core::Scheduler task at
             * AP_IDLE_CHECK_PERIOD_MS) — a no-op unless a timeout above
             * has actually elapsed. */
            void tick(uint32_t now_ms);

            /** Restarts the idle timeout -- call whenever the phone does
             * something, so a session that is still editing isn't cut off
             * AP_IDLE_TIMEOUT_MS after the AP came up. No-op unless
             * Active. */
            void note_activity(uint32_t now_ms);

            ApState state() const
            {
                return state_;
            }
            const char *ssid() const
            {
                return ssid_;
            }
            const char *ip_text() const
            {
                return ip_text_;
            }

        private:
            RemoteConfigLink &link_;
            IClock           &clock_;

            ApState  state_            = ApState::Idle;
            uint32_t state_entered_ms_ = 0;

            char ssid_[33]    = "";
            char ip_text_[16] = "";
    };

} // namespace remote::server
