/**
 * @file dash_s3_ota.hpp
 * @brief The firmware updater: the relay-side half of OTA (see
 * OTA_PLAN.md), plugged into esp32::RelayApp as a RelayExtension so it
 * shares the config site's SoftAP and web server. Adds:
 *
 *   GET  /update              the upload page (web/update.html)
 *   GET  /api/version         the running firmware's version, as text --
 *                             also how the config page knows to link here
 *   POST /api/update/request  asks the panel for update mode
 *   GET  /api/update/status   "off", "pending", "declined" or "armed"
 *   POST /update              the image itself, as the raw request body
 *
 * Uploads are refused outside update mode, which only the panel can turn
 * on (CMD_OTA_ARM, once someone taps Start on its "Update firmware?"
 * popup).
 *
 * The body is streamed straight into the inactive app slot, nothing is
 * buffered beyond its first FIRMWARE_ID_END bytes, which are held back
 * until the image has been identified (dash_s3_check_image()), so a wrong
 * file is refused before anything is erased. esp_ota_end() then checks
 * the image's checksum and appended SHA-256.
 *
 * None of that changes what boots. The panel is told how it's going
 * (EVT_OTA_BEGIN/PROGRESS/RESULT) and the slot is only switched on
 * CMD_OTA_COMMIT, which the dash sends once someone taps Install there.
 * Arduino's Update library is deliberately not used: its end() switches the
 * boot slot itself.
 *
 * Runs entirely on the relay task, like the rest of RelayApp.
 */
#pragma once

#include <WebServer.h>

#include "dash_s3_firmware.hpp"
#include "esp_ota_ops.h"
#include "relay_app.hpp"
#include "remote_protocol.hpp"

class DashS3Updater
    : public esp32::RelayExtension
    , private RequestHandler
{
    public:
        void attach(WebServer &server, esp32::RelayLink &link) override;
        void on_frame(remote::protocol::MsgType type,
                      const uint8_t            *payload,
                      uint16_t                  len) override;

    private:
        /**
         * RequestHandler, for POST /update. A handler of its own rather
         * than server.on(): that routes a multipart body to the same
         * callback as a raw one, and server.raw() is null for the
         * former. This one takes raw bodies only -- a multipart post is
         * parsed and discarded by WebServer, then refused in handle().
         */
        bool canHandle(HTTPMethod method, String uri) override;
        bool canRaw(String uri) override;
        void raw(WebServer &server, String uri, HTTPRaw &raw) override;
        bool handle(WebServer &server, HTTPMethod method, String uri) override;

        void handle_page();
        void handle_version();
        void handle_mode_request();
        void handle_mode_status();

        void start_upload(size_t content_length);
        void take_bytes(const uint8_t *data, size_t len);
        /**
         * @returns False (having failed the upload) if the flash write
         * didn't take.
         */
        bool write_image(const uint8_t *data, size_t len);
        void end_upload();
        void abort_upload();
        /**
         * Fails the upload as `result`, unless it already has: drops a
         * half-written image, and tells the panel if it was told the
         * upload began. The rest of the body is then drained unread.
         */
        void fail(remote::protocol::OtaResult result);
        void send_result(remote::protocol::OtaResult result);
        /**
         * Undoes start_upload()'s disableCore0WDT() -- see there.
         */
        void restore_idle_watchdog();
        void commit();

        WebServer        *server_ = nullptr;
        esp32::RelayLink *link_   = nullptr;

        /**
         * Update mode, as the phone sees it. Declined sticks until the
         * phone asks again, so a poll can't miss it.
         */
        enum class Mode : uint8_t
        {
            Off,
            Pending,
            Declined,
            Armed,
        };
        Mode mode_ = Mode::Off;

        /**
         * The upload in progress. `result_` stays Ok until something
         * fails. `body_seen_` tells handle() a raw body came with this
         * request, as opposed to a form WebServer parsed itself.
         */
        bool                        body_seen_         = false;
        bool                        ota_begun_         = false;
        bool                        idle_watchdog_off_ = false;
        remote::protocol::OtaResult result_ = remote::protocol::OtaResult::Ok;
        const esp_partition_t      *slot_   = nullptr;
        esp_ota_handle_t            ota_handle_ = 0;
        uint32_t                    total_      = 0;
        uint32_t                    written_    = 0;
        uint32_t                    reported_   = 0;
        uint8_t                     head_[FIRMWARE_ID_END];
        size_t                      head_len_                      = 0;
        char                        version_[FIRMWARE_VERSION_LEN] = "";

        /**
         * A verified image waiting on CMD_OTA_COMMIT; null when there's
         * none. Any new upload clears it -- it overwrites the same slot.
         */
        const esp_partition_t *ready_slot_                          = nullptr;
        char                   ready_version_[FIRMWARE_VERSION_LEN] = "";
};
