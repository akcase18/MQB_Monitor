/**
 * @file config_web.hpp
 * @brief Phone-facing wireless config form. Relays GET "/" into a
 * REQ_GET_STATE round trip (RelayLink::send_request_and_wait()) and
 * renders the result as a plain server-rendered HTML <form> — see the
 * Phase 5 plan (immutable-jingling-balloon.md) for why this is
 * form-urlencoded POST rather than a JSON/JS app. POST "/save" parses the
 * submitted body straight into the wire structs and relays a
 * REQ_APPLY_CONFIG, then redirects back to "/" (POST-redirect-GET) with a
 * status banner.
 *
 * remote::protocol's wire structs are reused directly (RelayLink already
 * links remote::protocol) rather than re-deriving a parallel schema here.
 * MAX_PANELS_CAP/MAX_SIGNALS_CAP below are *not* assumed equal to
 * core::MAX_PANELS/core::SIGNAL_CATALOG_COUNT -- platform/esp32 has zero
 * core:: dependency (see unit_labels.hpp's file doc comment for why); the
 * real counts come off the wire on every request (RESP_STATE's
 * panel_count/signal_count fields) and these caps only bound local
 * array/stack sizing against a malformed reply.
 */
#pragma once

#include <WebServer.h>

#include "config_page_render.hpp"
#include "relay_link.hpp"
#include "remote_protocol.hpp"

namespace esp32
{
    constexpr uint8_t MAX_PANELS_CAP  = 8;
    constexpr uint8_t MAX_SIGNALS_CAP = 40;

    class ConfigWebHandler
    {
        public:
            explicit ConfigWebHandler(RelayLink &relay);

            /** Registers GET "/" and POST "/save" on `server`. Must be
             * called before `server` starts handling requests. */
            void attach(WebServer &server);

        private:
            void handle_get();
            void handle_post();

            /** Sends REQ_GET_STATE and decodes the RESP_STATE reply into
             * the settings_/panels_/catalog_/signal_cfg_ members below.
             * Any wire panel/signal count beyond MAX_PANELS_CAP/
             * MAX_SIGNALS_CAP is still fully consumed (to keep the decode
             * cursor in sync for the rest of the payload) but not stored.
             *
             * @returns false on a relay timeout/desync -- callers should
             * render an error page rather than a half-populated form.
             */
            bool fetch_state();

            /** Streams the full HTML page for the currently-populated
             * settings_/panels_/catalog_/signal_cfg_ state via chunked
             * server.sendContent() calls -- deliberately not one giant
             * String, to keep peak heap use down for a ~130-field form.
             * `status_msg` (nullable) renders as a banner at the top. Thin
             * wrapper around render_config_page() (config_page_render.hpp)
             * -- see that file's doc comment for why the actual markup
             * lives there instead of here.
             */
            void render_page(const char *status_msg);

            static void webserver_write_cb(void *ctx, const char *chunk);

            RelayLink &relay_;
            WebServer *server_    = nullptr;
            bool       is_metric_ = true;

            remote::protocol::SettingsWire settings_ {};

            uint8_t                     panel_count_ = 0;
            remote::protocol::PanelWire panels_[MAX_PANELS_CAP] {};

            uint16_t signal_count_ = 0;
            remote::protocol::SignalCatalogEntryWire
                                               catalog_[MAX_SIGNALS_CAP] {};
            remote::protocol::SignalConfigWire signal_cfg_[MAX_SIGNALS_CAP] {};
    };

} // namespace esp32
