#include "remote_ap_controller.hpp"

#include <stdio.h>
#include <string.h>

namespace remote::server
{
    using protocol::ApEnableWire;
    using protocol::ApReadyWire;
    using protocol::MsgType;

    namespace
    {
        constexpr const char *AP_SSID = "MQB-Monitor-Setup";
    } // namespace

    FLASHMEM RemoteApController::RemoteApController(RemoteConfigLink &link,
                                                    IClock           &clock)
        : link_(link)
        , clock_(clock)
    {
    }

    FLASHMEM void RemoteApController::activate()
    {
        /* Also allowed from Deactivating: WirelessSetupScreen's close (X)
         * button both deactivates and returns to Settings optimistically,
         * without waiting for the deactivation round trip (see its file
         * doc comment) — reopening the screen within that window (a real
         * user workflow, not just a race, and the *only* way to leave the
         * screen now that there's no separate Turn Off control) would
         * otherwise no-op here and leave the screen stuck on the spinner
         * for up to AP_DISABLE_GRACE_MS with nothing left to ever
         * retrigger activation. Sending CMD_AP_ENABLE again here just
         * supersedes the in-flight CMD_AP_DISABLE — the C3 handles a fresh
         * enable the same way regardless of AP state. */
        if (state_ != ApState::Idle && state_ != ApState::Deactivating)
        {
            return;
        }

        snprintf(ssid_, sizeof(ssid_), "%s", AP_SSID);
        ip_text_[0] = '\0';

        /* No password — see the file doc comment. wire.password stays
         * empty; ApManager::enable() on the C3 treats that as "open AP". */
        ApEnableWire wire {};
        snprintf(wire.ssid, sizeof(wire.ssid), "%s", ssid_);

        uint8_t  payload[sizeof(ApEnableWire::ssid)
                        + sizeof(ApEnableWire::password)];
        uint8_t *end = protocol::encode_ap_enable(payload, wire);
        link_.send_command(MsgType::CMD_AP_ENABLE,
                           payload,
                           static_cast<uint16_t>(end - payload));

        state_            = ApState::Activating;
        state_entered_ms_ = clock_.millis();
    }

    FLASHMEM void RemoteApController::deactivate()
    {
        if (state_ != ApState::Active)
        {
            return;
        }

        link_.send_command(MsgType::CMD_AP_DISABLE, nullptr, 0);
        state_            = ApState::Deactivating;
        state_entered_ms_ = clock_.millis();
    }

    FLASHMEM void RemoteApController::on_event(MsgType        type,
                                               const uint8_t *payload,
                                               uint16_t       len,
                                               uint32_t       now_ms)
    {
        switch (type)
        {
            case MsgType::EVT_AP_READY: {
                if (state_ != ApState::Activating)
                {
                    return;
                }

                ApReadyWire wire {};
                protocol::decode_ap_ready(payload, wire);
                snprintf(ip_text_,
                         sizeof(ip_text_),
                         "%u.%u.%u.%u",
                         wire.ip[0],
                         wire.ip[1],
                         wire.ip[2],
                         wire.ip[3]);

                state_            = ApState::Active;
                state_entered_ms_ = now_ms;
                stations_         = 0;
                break;
            }

            case MsgType::EVT_AP_STATIONS:
                if (state_ != ApState::Active || len < 1)
                {
                    return;
                }
                /* A phone joining or leaving counts as activity, so the
                 * idle timeout runs from the last of either. */
                stations_         = payload[0];
                state_entered_ms_ = now_ms;
                break;

            case MsgType::EVT_AP_DISABLED:
                if (state_ == ApState::Deactivating)
                {
                    state_ = ApState::Idle;
                }
                break;

            default:
                break;
        }
    }

    FLASHMEM void RemoteApController::note_activity(uint32_t now_ms)
    {
        if (state_ == ApState::Active)
        {
            state_entered_ms_ = now_ms;
        }
    }

    FLASHMEM void RemoteApController::tick(uint32_t now_ms)
    {
        switch (state_)
        {
            case ApState::Activating:
                if (now_ms - state_entered_ms_ > AP_ACTIVATE_TIMEOUT_MS)
                {
                    /* C3 never answered — give up rather than staying
                     * "Activating" forever. */
                    state_ = ApState::Idle;
                }
                break;

            case ApState::Active:
                if (now_ms - state_entered_ms_ > AP_IDLE_TIMEOUT_MS)
                {
                    deactivate();
                }
                break;

            case ApState::Deactivating:
                if (now_ms - state_entered_ms_ > AP_DISABLE_GRACE_MS)
                {
                    /* EVT_AP_DISABLED never arrived — assume it's down
                     * anyway rather than staying wedged. */
                    state_ = ApState::Idle;
                }
                break;

            case ApState::Idle:
            default:
                break;
        }
    }

} // namespace remote::server
