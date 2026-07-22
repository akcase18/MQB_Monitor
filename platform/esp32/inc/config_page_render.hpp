/**
 * @file config_page_render.hpp
 * @brief Pure HTML rendering for the phone config form -- takes already-
 * decoded wire state in, writes HTML chunks out via a plain callback, and
 * has zero Arduino/WebServer/WiFi dependency. Extracted out of
 * ConfigWebHandler::render_page() (config_web.cpp) specifically so it has
 * exactly one implementation shared by both real callers rather than two
 * that could silently drift apart:
 *   - config_web.cpp: the real ESP32 firmware, writing chunks to a
 *     WebServer's chunked HTTP response.
 *   - platform/esp32/tools/preview_main.cpp: a host-native layout-preview
 *     tool (env:web_preview) that calls this with fabricated sample data
 *     and writes the result to a static .html file -- no WebServer, no
 *     RelayLink, no hardware, just a visual check of the actual markup.
 */
#pragma once

#include <stdint.h>

#include "remote_protocol.hpp"

namespace esp32
{
    /** Chunk-sink callback -- render_config_page() calls this once per
     * HTML fragment rather than building one giant string in memory, so
     * a WebServer caller can stream it out via sendContent(). `ctx` is
     * opaque, passed through to every call unchanged. */
    using PageWriteFn = void (*)(void *ctx, const char *chunk);

    /**
     * Renders the phone config form for the given already-decoded
     * RESP_STATE contents plus display options, writing HTML chunks to
     * `write`. Every array is `count`-sized regardless of any compile-
     * time constant on either end of the wire link (see ConfigWebHandler's
     * file doc comment on why counts always come from the wire).
     */
    void render_config_page(
        const remote::protocol::SettingsWire           &settings,
        const remote::protocol::PanelWire              *panels,
        uint8_t                                         panel_count,
        const remote::protocol::SignalCatalogEntryWire *catalog,
        const remote::protocol::SignalConfigWire       *signal_cfg,
        uint16_t                                        signal_count,
        bool                                            is_metric,
        const char                                     *status_msg,
        uint32_t                                        client_count,
        PageWriteFn                                     write,
        void                                           *write_ctx);

} // namespace esp32
