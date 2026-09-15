#include "theme.hpp"

extern "C"
{
    /**
     * JetBrains Mono (SIL OFL 1.1) — one family for the numeric readouts
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

    /**
     * Font Awesome Free Solid (SIL OFL 1.1, license in
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
            lv_palette_main(LV_PALETTE_RED),       /* rec_indicator */
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
            /**
             * unit_stacked. This product's panels are wide enough for the
             * tag to sit beside the value, which is where it has always
             * been. */
            false,
        },
    };

    /**
     * The gauge product: a 320x172 SPI strip, three equal panels, no
     * touchscreen. Same nine JetBrains faces as THEME_DEFAULT -- this is a
     * remapping of the semantic ladder, not a new font generation.
     *
     * The data faces were chosen by arithmetic, not by eye. A 3-up Strip
     * column on this panel is
     *
     *     320 - 2*screen_margin(3) - 2*panel_gap(3) = 308, / 3 = 102 px
     *     102 - 2*panel_border_width(2)             =  98 px usable
     *
     * and the worst-case value is **five characters**, not three: Pressure
     * carries two decimals in metric and one in imperial (see
     * unit_decimals()), so oil pressure reads "10.34" bar or "150.0" psi.
     * jetbrains_mono_bold_46 advances 27.62 px/char and needs 138 px for
     * that -- it would clip. bold_30 advances 18.00 and needs 90, which
     * fits with room for the border. An earlier draft of the build plan
     * recommended bold_46 on a three-character assumption; measuring the
     * fonts is what corrected it, and test_gui_theme now pins it.
     *
     * The zoom view is the opposite story and the plan had it right:
     * one panel across 310 px of usable width, where bold_96 needs 288 for
     * five characters. That is the one place the largest face earns its
     * place, and it is why it was retained.
     */
    const Theme THEME_GAUGE = {
        /* palette -- unchanged; the reskin here is geometry, not color */
        THEME_DEFAULT.palette,
        /* typography */
        {
            &jetbrains_mono_bold_30, /* data_sm   -- 3-up panel */
            &jetbrains_mono_bold_30, /**
                                      * data_lg   -- every Strip panel is
                                      * the same size, so there is no
                                      * second tier to distinguish */
            &jetbrains_mono_bold_56, /* data_hero -- zoom, with a bar */
            &jetbrains_mono_bold_96, /* data_mega -- zoom, bare digits */
            &jetbrains_mono_14,      /* label_sm */
            &jetbrains_mono_14,      /* label_lg */
            &jetbrains_mono_14,      /* unit */
            &jetbrains_mono_14,      /* ui */
            &lv_font_montserrat_32,  /* symbol_lg */
            &jetbrains_mono_bold_24, /* title */
            &icon_day_night_28,
        },
        /* metrics */
        {
            320,
            172,
            6, /* panel_radius -- softer than 10 on a 102px-wide panel */
            2,
            3,
            3,
            /**
             * panel_height_sm_pct / _lg_pct are unused in LayoutStyle::
             * Strip, where every panel spans the full height. Set to an
             * even split rather than carried over from THEME_DEFAULT's
             * 0.45/0.55, so nothing reads as a meaningful ratio here. */
            0.5f,
            0.5f,
            /**
             * unit_stacked. A 102px column cannot hold "10.34" and "bar"
             * side by side at any font worth reading -- see Metrics. */
            true,
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
        THEME_DEFAULT.metrics.unit_stacked,
    };

    /**
     * Values match LVGL's built-in palette mains (lv_palette_main()) so a
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
