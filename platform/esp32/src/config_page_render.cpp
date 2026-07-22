#include "config_page_render.hpp"

#include <stdio.h>

#include "unit_labels.hpp"

namespace esp32
{
    namespace protocol = remote::protocol;

    namespace
    {
        void format_hex_color(char *out, size_t cap, uint32_t color)
        {
            snprintf(out, cap, "#%06lX", static_cast<unsigned long>(color));
        }

        uint8_t accent_preset_index(uint32_t color)
        {
            for (uint8_t i = 0; i < ACCENT_PRESET_COUNT; i++)
            {
                if (ACCENT_PRESETS[i] == color)
                {
                    return i;
                }
            }
            return 0;
        }
    } // namespace

    void render_config_page(const protocol::SettingsWire           &settings,
                            const protocol::PanelWire              *panels,
                            uint8_t                                 panel_count,
                            const protocol::SignalCatalogEntryWire *catalog,
                            const protocol::SignalConfigWire       *signal_cfg,
                            uint16_t    signal_count,
                            bool        is_metric,
                            const char *status_msg,
                            uint32_t    client_count,
                            PageWriteFn write,
                            void       *write_ctx)
    {
        char buf[900];

        char accent_hex[8];
        format_hex_color(accent_hex, sizeof(accent_hex), settings.accent_color);

        write(write_ctx,
              "<!doctype html><html><head><meta charset=\"utf-8\">"
              "<meta name=\"viewport\" "
              "content=\"width=device-width,initial-scale=1\">"
              "<meta name=\"color-scheme\" content=\"light dark\">"
              "<title>MQB Monitor Config</title><style>"
              ":root{--bg:#f4f5f7;--card:#ffffff;--text:#1a1d21;"
              "--muted:#6b7280;--border:#e2e4e8}"
              "@media(prefers-color-scheme:dark){:root{--bg:#131518;"
              "--card:#1d2024;--text:#edeef0;--muted:#9aa1ab;"
              "--border:#2c3036}}"
              "*{box-sizing:border-box}"
              "body{margin:0;padding:0 0 56px;background:var(--bg);"
              "color:var(--text);font:16px/1.5 -apple-system,"
              "BlinkMacSystemFont,\"Segoe UI\",Roboto,sans-serif}"
              "header{background:var(--accent);color:#fff;"
              "padding:22px 20px 18px}"
              "header h1{margin:0;font-size:1.3rem;font-weight:600}"
              "header p{margin:.35em 0 0;font-size:.85rem;opacity:.85}"
              "main{max-width:640px;margin:0 auto;padding:16px}"
              ".banner{background:var(--card);border-left:4px solid "
              "var(--accent);padding:.8em 1em;border-radius:8px;"
              "margin-bottom:16px;font-size:.92rem;box-shadow:0 1px 3px "
              "rgba(0,0,0,.06)}"
              "section{background:var(--card);border:1px solid "
              "var(--border);border-radius:12px;padding:18px;"
              "margin-bottom:16px}"
              "section>h2{margin:0 0 12px;font-size:.8rem;font-weight:700;"
              "text-transform:uppercase;letter-spacing:.04em;"
              "color:var(--muted)}"
              ".field{display:flex;align-items:center;"
              "justify-content:space-between;gap:14px;padding:10px 0;"
              "border-bottom:1px solid var(--border)}"
              ".field:last-child{border-bottom:none}"
              ".field>label{font-size:.95rem}"
              "input[type=number],select{font-size:1rem;padding:8px 10px;"
              "border:1px solid var(--border);border-radius:8px;"
              "background:var(--bg);color:var(--text)}"
              ".field input[type=number]{width:5.5em;text-align:right}"
              ".radio-group{display:flex;gap:6px;flex-wrap:wrap}"
              ".radio-group label{display:flex;align-items:center;gap:4px;"
              "font-size:.85rem;padding:6px 12px;border:1px solid "
              "var(--border);border-radius:20px}"
              "input[type=radio],input[type=checkbox]{accent-color:"
              "var(--accent)}"
              ".color-picker{display:flex;gap:10px;flex-wrap:wrap}"
              ".color-picker input{position:absolute;opacity:0;width:0;"
              "height:0}"
              ".color-swatch{display:inline-block;width:2em;height:2em;"
              "border-radius:50%;cursor:pointer;border:3px solid "
              "transparent;box-sizing:border-box}"
              ".color-picker input:checked+.color-swatch{border-color:"
              "var(--text);box-shadow:0 0 0 1px var(--border)}"
              ".card{border:1px solid var(--border);border-radius:10px;"
              "padding:14px;margin-bottom:12px}"
              ".card:last-child{margin-bottom:0}"
              ".card>h3{margin:0 0 10px;font-size:.75rem;font-weight:700;"
              "color:var(--muted);text-transform:uppercase;"
              "letter-spacing:.04em}"
              ".units-toggle{display:flex;gap:8px;margin-bottom:14px}"
              ".units-toggle a{flex:1;text-align:center;padding:8px;"
              "border-radius:8px;border:1px solid var(--border);"
              "text-decoration:none;color:var(--text);font-size:.9rem}"
              ".units-toggle a.active{background:var(--accent);"
              "border-color:var(--accent);color:#fff;font-weight:600}"
              ".sig-name{font-weight:600;font-size:.95rem}"
              ".sig-meta{color:var(--muted);font-size:.8rem;margin:.15em 0 "
              "10px}"
              ".thresholds{display:flex;flex-wrap:wrap;gap:8px 16px}"
              ".t-field{display:flex;align-items:center;gap:6px;"
              "font-size:.85rem;color:var(--muted)}"
              ".t-field input[type=checkbox]{width:18px;height:18px}"
              ".t-field input[type=number]{width:5em;padding:6px 8px;"
              "font-size:.9rem}"
              "details>summary{cursor:pointer;font-weight:600;"
              "color:var(--accent);padding:6px 0 2px;list-style:none}"
              "details>summary::-webkit-details-marker{display:none}"
              "details>summary::before{content:\"\\25B8\\0020\"}"
              "details[open]>summary::before{content:\"\\25BE\\0020\"}"
              "button[type=submit]{position:sticky;bottom:14px;"
              "display:block;width:100%;background:var(--accent);"
              "color:#fff;border:none;border-radius:10px;padding:15px;"
              "font-size:1.05rem;font-weight:600;margin-top:4px;"
              "box-shadow:0 4px 14px rgba(0,0,0,.18)}"
              "</style>");
        snprintf(buf,
                 sizeof(buf),
                 "<style>:root{--accent:%s}</style></head><body>",
                 accent_hex);
        write(write_ctx, buf);

        snprintf(buf,
                 sizeof(buf),
                 "<header><h1>MQB Monitor</h1>"
                 "<p>Connected clients: %u</p></header><main>",
                 client_count);
        write(write_ctx, buf);

        if (status_msg != nullptr)
        {
            snprintf(
                buf, sizeof(buf), "<div class=\"banner\">%s</div>", status_msg);
            write(write_ctx, buf);
        }

        write(write_ctx, "<form method=\"post\" action=\"/save\">");

        snprintf(buf,
                 sizeof(buf),
                 "<input type=\"hidden\" name=\"panel_count\" value=\"%u\">"
                 "<input type=\"hidden\" name=\"signal_count\" value=\"%u\">"
                 "<input type=\"hidden\" name=\"units\" value=\"%s\">",
                 panel_count,
                 signal_count,
                 is_metric ? "metric" : "imperial");
        write(write_ctx, buf);

        /* Global Settings */
        write(write_ctx, "<section><h2>Global Settings</h2>");
        snprintf(buf,
                 sizeof(buf),
                 "<div class=\"field\"><label>Day brightness</label>"
                 "<input type=\"number\" min=\"0\" max=\"100\" "
                 "name=\"day_brightness\" value=\"%u\"></div>"
                 "<div class=\"field\"><label>Night brightness</label>"
                 "<input type=\"number\" min=\"0\" max=\"100\" "
                 "name=\"night_brightness\" value=\"%u\"></div>",
                 settings.day_brightness,
                 settings.night_brightness);
        write(write_ctx, buf);

        write(write_ctx,
              "<div class=\"field\"><label>Panel count</label>"
              "<select name=\"num_panels\">");
        for (uint8_t i = 1; i <= panel_count; i++)
        {
            snprintf(buf,
                     sizeof(buf),
                     "<option value=\"%u\"%s>%u</option>",
                     i,
                     i == settings.num_panels ? " selected" : "",
                     i);
            write(write_ctx, buf);
        }
        write(write_ctx, "</select></div>");

        snprintf(buf,
                 sizeof(buf),
                 "<div class=\"field\"><label>Orientation</label>"
                 "<div class=\"radio-group\">"
                 "<label><input type=\"radio\" name=\"orientation\" "
                 "value=\"0\"%s> Horizontal</label>"
                 "<label><input type=\"radio\" name=\"orientation\" "
                 "value=\"1\"%s> Vertical</label></div></div>",
                 settings.orientation == 0 ? " checked" : "",
                 settings.orientation == 1 ? " checked" : "");
        write(write_ctx, buf);

        uint8_t current_accent = accent_preset_index(settings.accent_color);
        write(write_ctx,
              "<div class=\"field\"><label>Accent color</label>"
              "<div class=\"color-picker\">");
        for (uint8_t i = 0; i < ACCENT_PRESET_COUNT; i++)
        {
            char hex[8];
            format_hex_color(hex, sizeof(hex), ACCENT_PRESETS[i]);
            /* A native <select> can't reliably show a colored swatch per
             * option across mobile browsers (iOS Safari in particular
             * ignores <option> background-color) -- this is the standard
             * no-JS swap: a hidden radio + a label styled as a solid
             * circle, with the CSS sibling selector below drawing a ring
             * around whichever one is :checked. */
            snprintf(buf,
                     sizeof(buf),
                     "<input type=\"radio\" name=\"accent_idx\" "
                     "value=\"%u\" id=\"accent%u\"%s>"
                     "<label for=\"accent%u\" class=\"color-swatch\" "
                     "style=\"background:%s\"></label>",
                     i,
                     i,
                     i == current_accent ? " checked" : "",
                     i,
                     hex);
            write(write_ctx, buf);
        }
        write(write_ctx, "</div></div></section>");

        /* Panels */
        snprintf(
            buf, sizeof(buf), "<section><h2>Panels (%u)</h2>", panel_count);
        write(write_ctx, buf);
        for (uint8_t i = 0; i < panel_count; i++)
        {
            snprintf(buf,
                     sizeof(buf),
                     "<div class=\"card\"><h3>Panel %u</h3>"
                     "<div class=\"field\"><label>Signal</label>"
                     "<select name=\"panel%u_signal\">",
                     i + 1,
                     i);
            write(write_ctx, buf);

            for (uint16_t j = 0; j < signal_count; j++)
            {
                snprintf(buf,
                         sizeof(buf),
                         "<option value=\"%u\"%s>%s</option>",
                         catalog[j].id,
                         catalog[j].id == panels[i].signal_id ? " selected"
                                                              : "",
                         catalog[j].name);
                write(write_ctx, buf);
            }
            write(write_ctx, "</select></div>");

            snprintf(buf,
                     sizeof(buf),
                     "<div class=\"field\"><label>Units</label>"
                     "<div class=\"radio-group\">"
                     "<label><input type=\"radio\" name=\"panel%u_units\" "
                     "value=\"0\"%s> Metric</label>"
                     "<label><input type=\"radio\" name=\"panel%u_units\" "
                     "value=\"1\"%s> Imperial</label></div></div>"
                     "<div class=\"field\"><label>Display</label>"
                     "<select name=\"panel%u_mode\">"
                     "<option value=\"0\"%s>Digital</option>"
                     "<option value=\"1\"%s>Bar</option>"
                     "<option value=\"2\"%s>Gauge</option>"
                     "</select></div></div>",
                     i,
                     panels[i].units == 0 ? " checked" : "",
                     i,
                     panels[i].units == 1 ? " checked" : "",
                     i,
                     panels[i].display_mode == 0 ? " selected" : "",
                     panels[i].display_mode == 1 ? " selected" : "",
                     panels[i].display_mode == 2 ? " selected" : "");
            write(write_ctx, buf);
        }
        write(write_ctx, "</section>");

        /* Signal Thresholds & Custom Range */
        write(write_ctx,
              "<section><h2>Signal Thresholds &amp; Custom Range"
              "</h2>");
        snprintf(buf,
                 sizeof(buf),
                 "<div class=\"units-toggle\">"
                 "<a href=\"/?units=metric\" class=\"%s\">Metric</a>"
                 "<a href=\"/?units=imperial\" class=\"%s\">Imperial</a>"
                 "</div>",
                 is_metric ? "active" : "",
                 !is_metric ? "active" : "");
        write(write_ctx, buf);

        snprintf(buf,
                 sizeof(buf),
                 "<details><summary>Show all signals (%u)</summary>",
                 signal_count);
        write(write_ctx, buf);

        for (uint16_t i = 0; i < signal_count; i++)
        {
            const protocol::SignalCatalogEntryWire &entry = catalog[i];
            const protocol::SignalConfigWire       &cfg   = signal_cfg[i];

            float native_min
                = is_metric ? entry.display_min
                            : unit_convert_to_imperial(entry.unit_type,
                                                       entry.display_min);
            float native_max
                = is_metric ? entry.display_max
                            : unit_convert_to_imperial(entry.unit_type,
                                                       entry.display_max);
            float lo         = is_metric
                                   ? cfg.lo
                                   : unit_convert_to_imperial(entry.unit_type, cfg.lo);
            float hi         = is_metric
                                   ? cfg.hi
                                   : unit_convert_to_imperial(entry.unit_type, cfg.hi);
            float custom_min = is_metric ? cfg.custom_min
                                         : unit_convert_to_imperial(
                                               entry.unit_type, cfg.custom_min);
            float custom_max = is_metric ? cfg.custom_max
                                         : unit_convert_to_imperial(
                                               entry.unit_type, cfg.custom_max);

            snprintf(buf,
                     sizeof(buf),
                     "<div class=\"card\"><div class=\"sig-name\">%s</div>"
                     "<div class=\"sig-meta\">%s &middot; range %.1f&ndash;"
                     "%.1f</div><div class=\"thresholds\">"
                     "<label class=\"t-field\"><input type=\"checkbox\" "
                     "name=\"sig%u_use_lo\"%s> Lo <input type=\"number\" "
                     "step=\"any\" name=\"sig%u_lo\" value=\"%.2f\"></label>"
                     "<label class=\"t-field\"><input type=\"checkbox\" "
                     "name=\"sig%u_use_hi\"%s> Hi <input type=\"number\" "
                     "step=\"any\" name=\"sig%u_hi\" value=\"%.2f\"></label>"
                     "<label class=\"t-field\"><input type=\"checkbox\" "
                     "name=\"sig%u_use_custom\"%s> Custom "
                     "<input type=\"number\" step=\"any\" "
                     "name=\"sig%u_custom_min\" value=\"%.2f\"> &ndash; "
                     "<input type=\"number\" step=\"any\" "
                     "name=\"sig%u_custom_max\" value=\"%.2f\"></label>"
                     "</div></div>",
                     entry.name,
                     unit_label(entry.unit_type, is_metric),
                     native_min,
                     native_max,
                     i,
                     cfg.use_lo != 0 ? " checked" : "",
                     i,
                     lo,
                     i,
                     cfg.use_hi != 0 ? " checked" : "",
                     i,
                     hi,
                     i,
                     cfg.use_custom_range != 0 ? " checked" : "",
                     i,
                     custom_min,
                     i,
                     custom_max);
            write(write_ctx, buf);
        }
        write(write_ctx, "</details></section>");

        write(write_ctx,
              "<button type=\"submit\">Save</button></form></main></body>"
              "</html>");
    }

} // namespace esp32
