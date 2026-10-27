/**
 * @file simulator_relay.hpp
 * @brief The simulator's ISerialLink: a stand-in for the dash's Wi-Fi
 * relay, in-process, answering remote/server the way the S3's relay
 * task does -- so the Wi-Fi and firmware-update screens can be laid out
 * and stepped through with no phone and no board.
 *
 * It brings the "AP" up and down on request (Settings -> Wi-Fi shows its
 * QR code), and fakes a phone from keys pressed in the simulator window,
 * all only while the AP is up:
 *
 *   P  a phone joins the AP, or leaves it (the Wi-Fi glyph goes from
 *      grey to the accent and back; leaving mid-preview returns to the
 *      QR code).
 *   V  the phone opens the config page: it reads the dash's config and
 *      previews it unchanged, with sample values, as the real page does
 *      on load. Joins first if P hasn't.
 *   H  the page goes away (switched from, locked, closed): the same
 *      REQ_REVERT_PREVIEW + REVERT_PAGE_LEFT the page's beacon sends.
 *   D  the page's Discard: an empty REQ_REVERT_PREVIEW, then a fresh
 *      preview.
 *   U  the phone asks for update mode (the "Update firmware?" popup),
 *      joining first if P hasn't.
 *   S  sends a firmware, once update mode is on: progress, then Install.
 *   F  sends one that fails partway.
 *
 * Install restarts the simulator, as the dash restarts into the update.
 * Nothing is installed. The fake page edits nothing -- it's for the
 * dash's side of a session (the banner, where each ending lands), not
 * for the page itself.
 */
#pragma once

#include <stdint.h>
#include <vector>

#include "platform.hpp"
#include "remote_protocol.hpp"

class SimulatorRelay : public ISerialLink
{
    public:
        static SimulatorRelay &instance();

        void   begin(uint32_t baud) override;
        size_t read(uint8_t *buf, size_t len) override;
        size_t write(const uint8_t *buf, size_t len) override;

        /** A key pressed in the simulator window, lower-cased -- fed by
         * SimulatorTouch::read(), which owns the SDL event queue. */
        void on_key(char key);

    private:
        SimulatorRelay() = default;

        void on_frame(const remote::protocol::DecodedFrame &frame,
                      uint32_t                              now);
        void send(remote::protocol::MsgType type,
                  const uint8_t            *payload = nullptr,
                  uint16_t                  len     = 0);
        void start_upload(bool fail, uint32_t now);
        void run_upload(uint32_t now);
        void join_phone();
        void open_page();
        void preview_from_state(const remote::protocol::DecodedFrame &state,
                                uint32_t                              now);

        remote::protocol::FrameReader reader_;
        std::vector<uint8_t>          to_dash_;

        bool     ap_up_         = false;
        bool     phone_joined_  = false;
        uint32_t ap_ready_at_   = 0; /* nonzero: EVT_AP_READY is due */
        bool     armed_         = false;
        bool     uploading_     = false;
        bool     upload_fails_  = false;
        uint32_t upload_start_  = 0;
        uint32_t reported_      = 0;
        bool     ready_         = false;
        uint32_t restart_at_    = 0; /* nonzero: Install was tapped */

        /* The fake config page. */
        bool                 page_open_       = false;
        bool                 awaiting_state_  = false;
        std::vector<uint8_t> preview_;        /* the last REQ_PREVIEW_CONFIG */
        uint32_t             last_preview_ms_ = 0;
};
