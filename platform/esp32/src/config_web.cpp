#include "config_web.hpp"

#include <WiFi.h>
#include <stdio.h>

#include "unit_labels.hpp"

namespace esp32
{
    using remote::protocol::MsgType;
    namespace protocol = remote::protocol;

    namespace
    {
        float form_float(WebServer &server, const char *name)
        {
            if (!server.hasArg(name))
            {
                return 0.0f;
            }
            String v = server.arg(name);
            return v.length() == 0 ? 0.0f : v.toFloat();
        }

        uint32_t form_uint(WebServer  &server,
                           const char *name,
                           uint32_t    fallback)
        {
            if (!server.hasArg(name))
            {
                return fallback;
            }
            String v = server.arg(name);
            return v.length() == 0 ? fallback
                                   : static_cast<uint32_t>(v.toInt());
        }
    } // namespace

    ConfigWebHandler::ConfigWebHandler(RelayLink &relay)
        : relay_(relay)
    {
    }

    void ConfigWebHandler::attach(WebServer &server)
    {
        server_ = &server;
        server.on("/", HTTP_GET, [this]() { handle_get(); });
        server.on("/save", HTTP_POST, [this]() { handle_post(); });
    }

    bool ConfigWebHandler::fetch_state()
    {
        static uint8_t resp_buf[protocol::FRAME_MAX_PAYLOAD];
        uint16_t       resp_len = 0;
        if (!relay_.send_request_and_wait(MsgType::REQ_GET_STATE,
                                          nullptr,
                                          0,
                                          MsgType::RESP_STATE,
                                          resp_buf,
                                          sizeof(resp_buf),
                                          resp_len))
        {
            return false;
        }

        const uint8_t *p = resp_buf;
        p                = protocol::decode_settings(p, settings_);

        uint8_t wire_panel_count = 0;
        p                        = protocol::get_u8(p, wire_panel_count);
        panel_count_ = wire_panel_count < MAX_PANELS_CAP ? wire_panel_count
                                                         : MAX_PANELS_CAP;
        for (uint8_t i = 0; i < wire_panel_count; i++)
        {
            protocol::PanelWire discard {};
            p = protocol::decode_panel(
                p, i < MAX_PANELS_CAP ? panels_[i] : discard);
        }

        uint16_t wire_signal_count = 0;
        p                          = protocol::get_u16(p, wire_signal_count);
        signal_count_ = wire_signal_count < MAX_SIGNALS_CAP ? wire_signal_count
                                                            : MAX_SIGNALS_CAP;
        for (uint16_t i = 0; i < wire_signal_count; i++)
        {
            protocol::SignalCatalogEntryWire discard {};
            p = protocol::decode_signal_catalog_entry(
                p, i < MAX_SIGNALS_CAP ? catalog_[i] : discard);
        }
        for (uint16_t i = 0; i < wire_signal_count; i++)
        {
            protocol::SignalConfigWire discard {};
            p = protocol::decode_signal_config(
                p, i < MAX_SIGNALS_CAP ? signal_cfg_[i] : discard);
        }

        return true;
    }

    void ConfigWebHandler::handle_get()
    {
        is_metric_ = server_->arg("units") != "imperial";

        if (!fetch_state())
        {
            server_->send(
                503, "text/plain", "Relay to Teensy timed out -- try again.");
            return;
        }

        String      status_arg = server_->arg("status");
        const char *status_msg = nullptr;
        if (status_arg == "ok")
        {
            status_msg = "Saved.";
        }
        else if (status_arg == "err")
        {
            status_msg = "Save rejected -- check the submitted values.";
        }
        else if (status_arg == "timeout")
        {
            status_msg = "Save timed out -- try again.";
        }

        render_page(status_msg);
    }

    void ConfigWebHandler::handle_post()
    {
        WebServer &server = *server_;

        /* Refetch rather than trust member state left over from whatever
         * GET rendered this form -- cheap (this link never runs on a hot
         * path), and guarantees catalog_[]'s unit_type entries (needed
         * below for the imperial->metric conversion) are current rather
         * than possibly stale from an earlier session's request. */
        if (!fetch_state())
        {
            server.sendHeader("Location", "/?status=timeout");
            server.send(303);
            return;
        }

        bool submitted_metric = server.arg("units") != "imperial";

        uint8_t panel_count
            = static_cast<uint8_t>(form_uint(server, "panel_count", 0));
        if (panel_count > MAX_PANELS_CAP)
        {
            panel_count = MAX_PANELS_CAP;
        }
        uint16_t signal_count
            = static_cast<uint16_t>(form_uint(server, "signal_count", 0));
        if (signal_count > MAX_SIGNALS_CAP)
        {
            signal_count = MAX_SIGNALS_CAP;
        }

        protocol::SettingsWire settings {};
        settings.day_brightness
            = static_cast<uint8_t>(form_uint(server, "day_brightness", 0));
        settings.night_brightness
            = static_cast<uint8_t>(form_uint(server, "night_brightness", 0));
        settings.num_panels
            = static_cast<uint8_t>(form_uint(server, "num_panels", 1));
        settings.orientation
            = static_cast<uint8_t>(form_uint(server, "orientation", 0));
        uint8_t accent_idx
            = static_cast<uint8_t>(form_uint(server, "accent_idx", 0));
        settings.accent_color
            = ACCENT_PRESETS[accent_idx < ACCENT_PRESET_COUNT ? accent_idx : 0];

        static uint8_t req_buf[protocol::FRAME_MAX_PAYLOAD];
        uint8_t       *p = req_buf;
        p                = protocol::encode_settings(p, settings);

        char field[24];
        for (uint8_t i = 0; i < panel_count; i++)
        {
            protocol::PanelWire pw {};
            snprintf(field, sizeof(field), "panel%u_signal", i);
            pw.signal_id = static_cast<uint16_t>(form_uint(server, field, 0));
            snprintf(field, sizeof(field), "panel%u_units", i);
            pw.units = static_cast<uint8_t>(form_uint(server, field, 0));
            snprintf(field, sizeof(field), "panel%u_mode", i);
            pw.display_mode = static_cast<uint8_t>(form_uint(server, field, 0));
            p               = protocol::encode_panel(p, pw);
        }

        p = protocol::put_u16(p, signal_count);

        for (uint16_t i = 0; i < signal_count; i++)
        {
            uint8_t unit_type = i < MAX_SIGNALS_CAP ? catalog_[i].unit_type : 0;

            protocol::SignalConfigWire sw {};
            snprintf(field, sizeof(field), "sig%u_use_lo", i);
            sw.use_lo = server.hasArg(field) ? 1 : 0;
            snprintf(field, sizeof(field), "sig%u_lo", i);
            float lo = form_float(server, field);
            snprintf(field, sizeof(field), "sig%u_use_hi", i);
            sw.use_hi = server.hasArg(field) ? 1 : 0;
            snprintf(field, sizeof(field), "sig%u_hi", i);
            float hi = form_float(server, field);
            snprintf(field, sizeof(field), "sig%u_use_custom", i);
            sw.use_custom_range = server.hasArg(field) ? 1 : 0;
            snprintf(field, sizeof(field), "sig%u_custom_min", i);
            float custom_min = form_float(server, field);
            snprintf(field, sizeof(field), "sig%u_custom_max", i);
            float custom_max = form_float(server, field);

            if (!submitted_metric)
            {
                lo         = unit_convert_to_metric(unit_type, lo);
                hi         = unit_convert_to_metric(unit_type, hi);
                custom_min = unit_convert_to_metric(unit_type, custom_min);
                custom_max = unit_convert_to_metric(unit_type, custom_max);
            }

            sw.lo         = lo;
            sw.hi         = hi;
            sw.custom_min = custom_min;
            sw.custom_max = custom_max;

            p = protocol::encode_signal_config(p, sw);
        }

        uint16_t req_len = static_cast<uint16_t>(p - req_buf);

        uint8_t  resp_buf[8];
        uint16_t resp_len = 0;
        bool     ok = relay_.send_request_and_wait(MsgType::REQ_APPLY_CONFIG,
                                               req_buf,
                                               req_len,
                                               MsgType::RESP_APPLY_RESULT,
                                               resp_buf,
                                               sizeof(resp_buf),
                                               resp_len);

        const char *status = "timeout";
        if (ok && resp_len >= 1)
        {
            /* ApplyResult::Ok == 0 (remote_protocol.hpp) */
            status = resp_buf[0] == 0 ? "ok" : "err";
        }

        String location = "/?status=";
        location += status;
        location += "&units=";
        location += submitted_metric ? "metric" : "imperial";
        server.sendHeader("Location", location);
        server.send(303);
    }

    void ConfigWebHandler::webserver_write_cb(void *ctx, const char *chunk)
    {
        static_cast<WebServer *>(ctx)->sendContent(chunk);
    }

    void ConfigWebHandler::render_page(const char *status_msg)
    {
        WebServer &server = *server_;

        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/html", "");

        render_config_page(settings_,
                           panels_,
                           panel_count_,
                           catalog_,
                           signal_cfg_,
                           signal_count_,
                           is_metric_,
                           status_msg,
                           WiFi.softAPgetStationNum(),
                           webserver_write_cb,
                           &server);
    }

} // namespace esp32
