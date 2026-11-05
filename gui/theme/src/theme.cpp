#include "theme.hpp"

extern "C"
{
    /* JetBrains Mono (SIL OFL 1.1) — one family for the numeric readouts
     * (Bold weight, digits-only subset), labels/UI text (Regular weight),
     * and nav bar titles (Bold weight, full charset). The readout sizes
     * are a ladder PanelWidget picks from per cell (DATA_FONTS below).
     * License lives in gui/assets/font-licenses/. */
    extern const lv_font_t jetbrains_mono_bold_30;
    extern const lv_font_t jetbrains_mono_bold_38;
    extern const lv_font_t jetbrains_mono_bold_54;
    extern const lv_font_t jetbrains_mono_bold_64;
    extern const lv_font_t jetbrains_mono_bold_80;
    extern const lv_font_t jetbrains_mono_bold_92;
    extern const lv_font_t jetbrains_mono_bold_128;
    extern const lv_font_t jetbrains_mono_11;
    extern const lv_font_t jetbrains_mono_14;
    extern const lv_font_t jetbrains_mono_16;
    extern const lv_font_t jetbrains_mono_24;
    extern const lv_font_t jetbrains_mono_bold_24;

    /* Font Awesome Free Solid (SIL OFL 1.1, license in
     * gui/assets/font-licenses/) — just the sun/moon glyphs (gui::
     * ICON_SUN/ICON_MOON), for SettingsScreen's Day/Night toggle. */
    extern const lv_font_t icon_day_night_28;

    /* Font Awesome 5 (same license) — just the four LV_SYMBOL_ glyphs
     * symbol_lg draws: close, right chevron, plus and minus. Cut the way
     * LVGL cuts its built-in fonts (lib/lvgl/scripts/built_in_font), so
     * they match the built-in symbols elsewhere; Montserrat 32 carried
     * them before, at ~10x the flash. */
    extern const lv_font_t symbols_32;
}

namespace gui
{

    namespace
    {
        /**
         * Typography::data. Each step is about what some cell needs:
         * 30/38 small cells (Gauge/Digital), 54/64 the 2-panel columns,
         * 80/92 hero-sized cells (Gauge/Digital), 128 the full-screen
         * landscape panel. tools/gen_value_fonts.py regenerates them.
         */
        const lv_font_t *const DATA_FONTS[] = {
            &jetbrains_mono_bold_30, &jetbrains_mono_bold_38,
            &jetbrains_mono_bold_54, &jetbrains_mono_bold_64,
            &jetbrains_mono_bold_80, &jetbrains_mono_bold_92,
            &jetbrains_mono_bold_128,
        };
    } // namespace

    /**
     * Palette, fonts, and metrics ported from the old include/styles.hpp /
     * Screen.hpp: black panels, white text, grey pressed-highlight, blue/red
     * low/high warning highlights; 45%/55% screen-height small/large panels
     * on a 480x320 panel.
     */
    const Theme THEME_DEFAULT = {
        /* palette */
        {
            lv_color_black(),                      /* bg */
            lv_color_white(),                      /* fg */
            lv_palette_main(LV_PALETTE_GREEN),     /* accent */
            /* Kept several shades off the palette main, because that is
             * exactly what ACCENT_PRESETS' blue and red are: a warning
             * paints the whole panel while the Bar/Gauge on top of it
             * stays accent-colored, and at darken(2) a red accent on a red
             * warning was near enough to read as one solid block. Darker
             * rather than lighter — the separation works in either
             * direction, but white readout text over a light blue/red
             * panel does not. */
            lv_palette_darken(LV_PALETTE_BLUE, 3), /* warn_lo */
            lv_palette_darken(LV_PALETTE_RED, 3),  /* warn_hi */
            lv_palette_darken(LV_PALETTE_GREY, 1), /* panel_border */
            lv_palette_main(LV_PALETTE_AMBER),     /* range_overflow */
            lv_palette_main(LV_PALETTE_RED),       /* rec_indicator */
        },
        /* typography */
        {
            DATA_FONTS,
            sizeof(DATA_FONTS) / sizeof(DATA_FONTS[0]),
            &jetbrains_mono_14,
            &jetbrains_mono_16,
            &jetbrains_mono_24,
            &jetbrains_mono_11,
            &jetbrains_mono_24,
            &symbols_32,
            &jetbrains_mono_bold_24,
            &icon_day_night_28,
            &lv_font_montserrat_18, /* LV_FONT_DEFAULT, so already linked */
            &jetbrains_mono_bold_24,
        },
        /* metrics */
        {
            480,
            320,
            10,
            2,
            3,
            3,
            0.45f,
            0.55f,
        },
    };

    const Metrics METRICS_PORTRAIT = {
        320,
        480,
        THEME_DEFAULT.metrics.panel_radius,
        THEME_DEFAULT.metrics.panel_border_width,
        THEME_DEFAULT.metrics.panel_gap,
        THEME_DEFAULT.metrics.screen_margin,
        THEME_DEFAULT.metrics.panel_height_sm_pct,
        THEME_DEFAULT.metrics.panel_height_lg_pct,
    };

    /* Values match LVGL's built-in palette mains (lv_palette_main()) so a
     * preset reads as the same color it would if picked via that API —
     * plain hex here rather than calling lv_palette_main() at static-init
     * time, since these need to be usable from core::Settings::
     * accent_color's default too (see config_store.cpp), which has no
     * lv_color_t/LVGL dependency at all. */
    const uint32_t ACCENT_PRESETS[ACCENT_PRESET_COUNT] = {
        0x4CAF50, /* green (THEME_DEFAULT's default accent) */
        0x00BCD4, /* cyan */
        0x2196F3, /* blue */
        0x9C27B0, /* purple */
        0xE91E63, /* pink */
        0xF44336, /* red */
        0xFF9800, /* orange */
        0xFFC107, /* amber */
        0x9E9E9E, /* grayscale */
    };

} // namespace gui
