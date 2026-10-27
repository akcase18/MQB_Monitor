/**
 * @file relay_app.hpp
 * @brief The whole relay, minus the transport: SoftAP up/down on
 * CMD_AP_ENABLE/CMD_AP_DISABLE with EVT_AP_READY/EVT_AP_DISABLED back,
 * and the phone configurator (ConfigWebHandler) relaying its REQ_*
 * config messages over the same link.
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
    /**
     * Board-specific additions to the relay -- more routes on the web
     * server, more frame types off the link. The S3 dash's firmware
     * updater (platform/dash_s3/src/dash_s3_ota.cpp) is one; the C3 has
     * none. Runs on the relay's own task, like everything else here.
     */
    class RelayExtension
    {
        public:
            virtual ~RelayExtension() = default;

            /** Registers routes on `server` and keeps `link` for sending
             * events. Called once, from RelayApp::begin(). */
            virtual void attach(WebServer &server, RelayLink &link) = 0;

            /** Offered every frame RelayApp doesn't handle itself. */
            virtual void on_frame(remote::protocol::MsgType type,
                                  const uint8_t            *payload,
                                  uint16_t                  len)
                = 0;
    };

    class RelayApp
    {
        public:
            /** `stream` is the link to the dash side: whatever carries
             * remote::protocol frames to and from remote::server.
             * `extension`, if any, must outlive this. */
            explicit RelayApp(Stream         &stream,
                              RelayExtension *extension = nullptr);

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
            void report_stations(uint32_t now_ms);

            static void on_frame(void                     *ctx,
                                 remote::protocol::MsgType type,
                                 const uint8_t            *payload,
                                 uint16_t                  len);

            ApManager        ap_;
            RelayLink        link_;
            WebServer        server_;
            ConfigWebHandler web_;
            RelayExtension  *extension_;

            uint8_t  reported_stations_  = 0;
            uint32_t stations_checked_ms_ = 0;
    };

} // namespace esp32
