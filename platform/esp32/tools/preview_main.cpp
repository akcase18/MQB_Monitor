/**
 * @file preview_main.cpp
 * @brief Host-native layout preview for the phone config form — no
 * hardware, no RelayLink, no WebServer: fabricates representative wire
 * state (using the real core::SIGNAL_CATALOG for signal names/units/
 * ranges, so the preview reflects actual content, not placeholder text)
 * and calls the exact same render_config_page() (config_page_render.hpp)
 * the real ESP32 firmware uses, writing the result to a static .html
 * file. Part of env:web_preview (platformio.ini) — see platform/esp32/
 * README.md for how to run it.
 */
#include <stdio.h>
#include <string.h>

#include <string>

#include "config_page_render.hpp"
#include "signal_catalog.hpp"

namespace
{
    void append_chunk(void *ctx, const char *chunk)
    {
        static_cast<std::string *>(ctx)->append(chunk);
    }
} // namespace

int main(int argc, char **argv)
{
    const char *out_path = argc > 1 ? argv[1] : "config_preview.html";

    remote::protocol::SettingsWire settings {};
    settings.day_brightness   = 100;
    settings.night_brightness = 20;
    settings.num_panels       = 5;
    settings.orientation      = 0;
    settings.accent_color     = 0x2196F3; /* blue preset */

    constexpr uint8_t           PANEL_COUNT = 6;
    remote::protocol::PanelWire panels[PANEL_COUNT] {};
    for (uint8_t i = 0; i < PANEL_COUNT; i++)
    {
        const core::SignalSpec &spec
            = core::SIGNAL_CATALOG[i % core::SIGNAL_CATALOG_COUNT];
        panels[i].signal_id    = spec.id;
        panels[i].units        = static_cast<uint8_t>(i % 2);
        panels[i].display_mode = static_cast<uint8_t>(i % 3);
    }

    constexpr uint16_t SIGNAL_COUNT = core::SIGNAL_CATALOG_COUNT;
    remote::protocol::SignalCatalogEntryWire catalog[SIGNAL_COUNT] {};
    remote::protocol::SignalConfigWire       signal_cfg[SIGNAL_COUNT] {};
    for (uint16_t i = 0; i < SIGNAL_COUNT; i++)
    {
        const core::SignalSpec &spec = core::SIGNAL_CATALOG[i];

        catalog[i].id = spec.id;
        strncpy(
            catalog[i].name, spec.name, remote::protocol::WIRE_NAME_LEN - 1);
        catalog[i].name[remote::protocol::WIRE_NAME_LEN - 1] = '\0';
        catalog[i].unit_type   = static_cast<uint8_t>(spec.unit_type);
        catalog[i].display_min = spec.display_min;
        catalog[i].display_max = spec.display_max;

        /* Vary which visual states show up: every other signal has a
         * warning threshold set, every third has a custom range too, so
         * the preview exercises checked/unchecked and filled/empty
         * fields rather than showing 22 identical blank rows. */
        float mid = (spec.display_min + spec.display_max) / 2.0f;
        if (i % 2 == 0)
        {
            signal_cfg[i].use_lo = 1;
            signal_cfg[i].lo
                = spec.display_min + (mid - spec.display_min) * 0.25f;
            signal_cfg[i].use_hi = 1;
            signal_cfg[i].hi     = mid + (spec.display_max - mid) * 0.75f;
        }
        if (i % 3 == 0)
        {
            signal_cfg[i].use_custom_range = 1;
            signal_cfg[i].custom_min       = spec.display_min;
            signal_cfg[i].custom_max       = mid;
        }
    }

    std::string html;
    esp32::render_config_page(settings,
                              panels,
                              PANEL_COUNT,
                              catalog,
                              signal_cfg,
                              SIGNAL_COUNT,
                              /* is_metric */ true,
                              "Saved. (sample banner for layout preview)",
                              /* client_count */ 2,
                              append_chunk,
                              &html);

    FILE *f = fopen(out_path, "w");
    if (f == nullptr)
    {
        fprintf(stderr, "Couldn't open %s for writing\n", out_path);
        return 1;
    }
    fwrite(html.data(), 1, html.size(), f);
    fclose(f);

    printf("Wrote %zu bytes to %s\n", html.size(), out_path);
    return 0;
}
