/**
 * @file remote_ota_controller.hpp
 * @brief The panel's half of a firmware update (platform/dash_s3/
 * OTA_PLAN.md):
 *
 *   Idle -> Requested -> Armed -> Receiving -> Ready -> Installing
 *
 * The phone asks for update mode (EVT_OTA_REQUEST); the panel asks the
 * person in the car, and only accept() puts the relay into update mode
 * (CMD_OTA_ARM) -- outside it the relay refuses uploads. From there the
 * relay does every flash operation itself and reports through
 * EVT_OTA_*; this tracks those reports for the update screen, and sends
 * CMD_OTA_COMMIT only from install(), the second tap. Those two taps are
 * the update's whole security gate (the AP may be open), so nothing else
 * here ever arms or commits.
 *
 * A failed upload drops back to Armed, so the phone can simply send the
 * file again; leave() ends update mode. Boards whose relay has no update
 * support never send an EVT_OTA_*, so on them this simply stays Idle.
 * Like RemoteApController, a reply that never comes falls back on a
 * timeout rather than wedging.
 */
#pragma once

#include <stdint.h>

#include "remote_config_link.hpp"
#include "remote_protocol.hpp"

namespace remote::server
{
    enum class OtaState : uint8_t
    {
        Idle,
        Requested,  /* the phone asked -- waiting on accept()/decline() */
        Armed,      /* update mode: the relay will take an upload */
        Receiving,  /* an upload is being written */
        Ready,      /* written and verified -- waiting on install()/leave() */
        Installing, /* commit sent; the board is about to restart */
    };

    /**
     * How long the "Update firmware?" popup waits for an answer before
     * declining on its own.
     */
    constexpr uint32_t OTA_REQUEST_TIMEOUT_MS = 30 * 1000;
    /**
     * How long update mode waits for an upload before giving up -- the
     * phone asks as soon as its update page opens, and may never send
     * anything. Restarts after a failed upload.
     */
    constexpr uint32_t OTA_WAIT_TIMEOUT_MS = 5 * 60 * 1000;
    /**
     * Generous next to the relay's own 5s socket timeout, which ends a
     * stalled upload with EVT_OTA_RESULT well before this -- this only
     * covers that result never arriving.
     */
    constexpr uint32_t OTA_STALL_TIMEOUT_MS = 15 * 1000;
    /**
     * Switching slots re-verifies the whole image (a SHA-256 over ~1.2MB)
     * before the relay restarts the board; far longer than that means the
     * commit was lost.
     */
    constexpr uint32_t OTA_INSTALL_TIMEOUT_MS = 10 * 1000;

    class RemoteOtaController
    {
        public:
            explicit RemoteOtaController(RemoteConfigLink &link);

            /**
             * Feed from RemoteConfigLink::poll()'s EventCallback. Ignores
             * everything but EVT_OTA_*.
             */
            void on_event(protocol::MsgType type,
                          const uint8_t    *payload,
                          uint16_t          len,
                          uint32_t          now_ms);

            /**
             * Call periodically -- a no-op unless a timeout above has
             * elapsed.
             */
            void tick(uint32_t now_ms);

            /**
             * Requested -> Armed: sends CMD_OTA_ARM.
             */
            void accept(uint32_t now_ms);

            /**
             * Requested -> Idle: sends CMD_OTA_DISARM.
             */
            void decline();

            /**
             * Ends update mode from Armed or Ready (the image, if any,
             * goes unused): sends CMD_OTA_DISARM. Mid-upload or
             * mid-install it does nothing -- there is no clean way out of
             * either, and both end on their own.
             */
            void leave();

            /**
             * Ready -> Installing: sends CMD_OTA_COMMIT. No-op in any
             * other state.
             */
            void install(uint32_t now_ms);

            OtaState state() const
            {
                return state_;
            }

            /**
             * Armed or past it: the update screen's states.
             */
            bool in_update_mode() const
            {
                return state_ >= OtaState::Armed;
            }

            /**
             * The incoming image's version. Valid from Receiving on.
             */
            const char *version() const
            {
                return version_;
            }

            /**
             * 0-100 through the upload; 0 when its size is unknown.
             */
            uint8_t percent() const;

            /**
             * How the last upload or install in this update mode ended
             * (Ok if none has).
             */
            protocol::OtaResult last_result() const
            {
                return last_result_;
            }

            /**
             * When the current state began -- the Requested countdown's
             * starting point.
             */
            uint32_t state_entered_ms() const
            {
                return state_entered_ms_;
            }

        private:
            void enter(OtaState state, uint32_t now_ms);
            /**
             * An upload or install failed: back to waiting for a file.
             */
            void fail(protocol::OtaResult result, uint32_t now_ms);
            void send(protocol::MsgType type);

            RemoteConfigLink &link_;

            OtaState state_            = OtaState::Idle;
            uint32_t state_entered_ms_ = 0;
            uint32_t last_progress_ms_ = 0;
            uint32_t written_          = 0;
            uint32_t total_            = 0;

            char                version_[protocol::WIRE_VERSION_LEN] = "";
            protocol::OtaResult last_result_ = protocol::OtaResult::Ok;
    };

} // namespace remote::server
