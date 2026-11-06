/**
 * @file config_web.hpp
 * @brief Phone-facing wireless configurator. Serves one static page
 * (platform/esp32/web/index.html, gzipped into the firmware at build time
 * by tools/web/embed_web.py) and a tiny relay API behind it:
 *
 *   GET  /api/state    REQ_GET_STATE       -> RESP_STATE payload, as hex
 *   POST /api/preview  REQ_PREVIEW_CONFIG  <- hex body, -> ApplyResult
 *   POST /api/save     REQ_APPLY_CONFIG    <- hex body, -> ApplyResult
 *   POST /api/revert   REQ_REVERT_PREVIEW               -> ApplyResult
 *
 * The C3 does no decoding of its own: the page encodes and decodes
 * remote::protocol's records itself and the Teensy validates every
 * batch, so this side is a pipe between HTTP and the UART frame link.
 * That keeps the C3 free of any copy of the Teensy's tables (units,
 * accent presets, the signal catalog) -- the page is the only consumer of
 * the decoded state, so the decoding lives with it.
 *
 * Hex rather than raw bytes because WebServer hands a POST body back as
 * an Arduino String, which is not safe to fill with NULs.
 */
#pragma once

#include <WebServer.h>

#include "relay_link.hpp"
#include "remote_protocol.hpp"

namespace esp32
{
    class ConfigWebHandler
    {
        public:
            explicit ConfigWebHandler(RelayLink &relay);

            /**
             * Registers every route above on `server`. Must be called
             * before `server` starts handling requests.
             */
            void attach(WebServer &server);

        private:
            void handle_page();
            void handle_state();
            /**
             * Relays the hex body of the current POST as `type` and
             * answers with the one-byte ApplyResult, as a decimal digit.
             */
            void handle_change(remote::protocol::MsgType type);

            RelayLink &relay_;
            WebServer *server_ = nullptr;
    };

} // namespace esp32
