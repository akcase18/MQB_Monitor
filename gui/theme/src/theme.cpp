#include "theme.hpp"

extern "C"
{
    /* JetBrains Mono (SIL OFL 1.1) — one family for the numeric readouts
     * (Bold weight, digits-only subset), labels/UI text (Regular weight),
     * and nav bar titles (Bold weight, full charset), sized from actual
     * panel-cell dimensions so a 4-character value ("2546") fits the
     * tightest cell (the 6-panel grid, ~150px wide) — and, for Gauge mode,
     * fits inside the arc alongside it — without overflowing.
     * License lives in gui/assets/font-licenses/. */
    extern const lv_font_t jetbrains_mono_bold_30;
    extern const lv_font_t jetbrains_mono_bold_46;
    extern const lv_font_t jetbrains_mono_bold_56;
    extern const lv_font_t jetbrains_mono_bold_96;
    extern const lv_font_t jetbrains_mono_11;
    extern const lv_font_t jetbrains_mono_14;
    extern const lv_font_t jetbrains_mono_16;
    extern const lv_font_t jetbrains_mono_24;
    extern const lv_font_t jetbrains_mono_bold_24;

    /* Font Awesome Free Solid (SIL OFL 1.1, license in
     * gui/assets/font-licenses/) — just the sun/moon glyphs (gui::
     * ICON_SUN/ICON_MOON), for SettingsScreen's Day/Night toggle. */
    extern const lv_font_t icon_day_night_28;
}

namespace gui
{

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
            lv_palette_darken(LV_PALETTE_BLUE, 2), /* warn_lo */
            lv_palette_darken(LV_PALETTE_RED, 2),  /* warn_hi */
            lv_palette_darken(LV_PALETTE_GREY, 1), /* panel_border */
            lv_palette_main(LV_PALETTE_AMBER),     /* range_overflow */
        },
        /* typography */
        {
            &jetbrains_mono_bold_30,
            &jetbrains_mono_bold_46,
            &jetbrains_mono_bold_56,
            &jetbrains_mono_bold_96,
            &jetbrains_mono_14,
            &jetbrains_mono_16,
            &jetbrains_mono_11,
            &jetbrains_mono_24,
            &lv_font_montserrat_32,
            &jetbrains_mono_bold_24,
            &icon_day_night_28,
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

    /**
     * Cooler, higher-contrast variant of THEME_DEFAULT — same fonts and
     * layout fractions, different palette and slightly roomier spacing.
     * Exists to prove a reskin only touches this file: nothing in
     * gui/widgets or gui/screens references THEME_ALT or THEME_DEFAULT by
     * name, only through the `const Theme &` passed in at init().
     */
    const Theme THEME_ALT = {
        /* palette */
        {
            lv_palette_darken(LV_PALETTE_BLUE_GREY, 4),  /* bg */
            lv_color_white(),                            /* fg */
            lv_palette_main(LV_PALETTE_CYAN),            /* accent */
            lv_palette_main(LV_PALETTE_LIGHT_BLUE),      /* warn_lo */
            lv_palette_main(LV_PALETTE_ORANGE),          /* warn_hi */
            lv_palette_lighten(LV_PALETTE_BLUE_GREY, 2), /* panel_border */
            lv_palette_main(LV_PALETTE_YELLOW),          /* range_overflow */
        },
        /* typography */
        {
            &jetbrains_mono_bold_30,
            &jetbrains_mono_bold_46,
            &jetbrains_mono_bold_56,
            &jetbrains_mono_bold_96,
            &jetbrains_mono_14,
            &jetbrains_mono_16,
            &jetbrains_mono_11,
            &jetbrains_mono_24,
            &lv_font_montserrat_32,
            &jetbrains_mono_bold_24,
            &icon_day_night_28,
        },
        /* metrics */
        {
            480,
            320,
            16,
            2,
            10,
            10,
            0.45f,
            0.55f,
        },
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
