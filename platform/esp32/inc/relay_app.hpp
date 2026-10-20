/**
 * @file relay_app.hpp
 * @brief The whole relay, minus the transport: SoftAP up/down on
 * CMD_AP_ENABLE/CMD_AP_DISABLE with EVT_AP_READY/EVT_AP_DISABLED back,
 * and the phone-facing config form (ConfigWebHandler) relaying
 * REQ_GET_STATE/REQ_APPLY_CONFIG over the same link.
 *
 * Two boards run it against two different Streams: the ESP32-C3 relay
 * (platform/esp32/src/main.cpp) over the UART to the Teensy, and the
 * ESP32-S3 dash (platform/dash_s3/src/dash_s3_wireless.cpp), which has
 * its own Wi-Fi and so runs this in-process over an in-memory pipe to
 * the same remote::server code the Teensy talks to. Neither end of the
 * protocol knows which it is.
 */
#pragma once

#include <Stream.h>
#include <WebServer.h>

#include "ap_manager.hpp"
#include "config_web.hpp"
#include "relay_link.hpp"

namespace esp32
{
    class RelayApp
    {
        public:
            /** `stream` is the link to the dash side: whatever carries
             * remote::protocol frames to and from remote::server. */
            explicit RelayApp(Stream &stream);

            /** Brings Wi-Fi to AP mode (idle, nothing broadcasting) and
             * starts the web server. Call once, from the task that will
             * then call poll() -- WiFi and WebServer stay on one task. */
            void begin();

            /** One iteration of the relay's loop: drain the link, serve
             * any HTTP client. Never overlaps RelayLink's blocking
             * request path, because that only runs inside a handler
             * this call invokes. */
            void poll(uint32_t now_ms);

        private:
            static void on_frame(void                     *ctx,
                                 remote::protocol::MsgType type,
                                 const uint8_t            *payload,
                                 uint16_t                  len);

            ApManager        ap_;
            RelayLink        link_;
            WebServer        server_;
            ConfigWebHandler web_;
    };

} // namespace esp32
