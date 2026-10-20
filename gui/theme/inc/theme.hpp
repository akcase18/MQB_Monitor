/**
 * @file theme.hpp
 * @brief Palette, typography, and metrics tokens for gui/widgets and
 * gui/screens. THEME_DEFAULT is the one reskin lever — a second theme
 * variant is just another `const Theme` with different values, touching
 * nothing outside gui/theme. Nothing in gui/widgets or gui/screens names
 * either constant; they only ever see the `const Theme &` passed to
 * init().
 */
#pragma once

#include <lvgl.h>

namespace gui
{

    struct Palette
    {
            lv_color_t bg;      /* screen / button background */
            lv_color_t fg;      /* primary text */
            lv_color_t accent;  /* pressed-button highlight */
            lv_color_t warn_lo; /* low-warning highlight */
            lv_color_t warn_hi; /* high-warning highlight */
            /* Panel outline — panels share the screen's own bg color, so
             * without a visible border a panel's bounds are only implied
             * by the gap between it and its neighbors. */
            lv_color_t panel_border;
            /* Bar/Gauge fill when the value is outside SignalSpec::
             * display_min/max — distinct from warn_lo/warn_hi (a
             * configured "this is bad" threshold) since a value can peg
             * a graphic at 100%/0% without ever crossing a warning. */
            lv_color_t range_overflow;
            /* InfoScreen's blinking capture indicator. Its own token
             * rather than reusing warn_hi: both are red by convention,
             * but a warning means "something is wrong" and this means
             * "recording", and a reskin should be free to keep the
             * recording light red while moving warnings elsewhere. */
            lv_color_t rec_indicator;
    };

    /**
     * symbol_lg must point at an LVGL built-in font (e.g.
     * lv_font_montserrat_32) — LV_SYMBOL_ glyphs live in Unicode's private-use
     * area and aren't present in the custom JetBrains Mono fonts, which were
     * only converted over their own printable/numeric ranges.
     */
    struct Typography
    {
            const lv_font_t *data_sm;   /* small-panel data readout */
            const lv_font_t *data_md;   /* small-panel bare Digital value —
                                         * no ring to fit inside, so it
                                         * can outgrow data_sm */
            const lv_font_t *data_lg;   /* large-panel data readout */
            const lv_font_t *data_xl;   /* large-panel bare Digital value,
                                         * when the panel is wide enough
                                         * (see panel_widget.cpp) */
            const lv_font_t *data_hero; /* Bar/Gauge value in a true hero
                                         * slot (count 1, count 3's hero) */
            const lv_font_t *data_mega; /* bare Digital value in a true
                                         * hero slot — as big as the
                                         * narrower of those slots (count
                                         * 3's hero) can fit */
            const lv_font_t *label_sm;  /* small-panel name label */
            const lv_font_t *label_lg;  /* large-panel name label */
            const lv_font_t *unit;      /* value's adjacent unit tag — its
                                         * own (smaller) size regardless of
                                         * panel tier, since even a small-
                                         * panel-sized font let a long unit
                                         * string ("lb-ft") push past the
                                         * panel's edge */
            const lv_font_t *ui;        /* general UI text (screens, buttons) */
            const lv_font_t *symbol_lg; /* done/settings buttons */
            const lv_font_t *title;     /* nav bar title — bold, sets it apart
                                         * from the plain-weight row labels
                                         * below it */
            const lv_font_t *icon_day_night; /* sun/moon glyphs (see
                                              * ICON_SUN/ICON_MOON below) —
                                              * a separate font from
                                              * symbol_lg since LVGL's
                                              * built-in symbol font has
                                              * nothing celestial in its
                                              * curated icon subset */
    };

    struct Metrics
    {
            uint16_t screen_width;
            uint16_t screen_height;
            uint8_t  panel_radius;
            uint8_t  panel_border_width;
            uint8_t  panel_gap;
            uint8_t  screen_margin; /* outer padding around the panel grid */
            float    panel_height_sm_pct; /* fraction of screen_height */
            float    panel_height_lg_pct; /* fraction of screen_height */
    };

    struct Theme
    {
            Palette    palette;
            Typography typography;
            Metrics    metrics;
    };

    extern const Theme THEME_DEFAULT;

    /** THEME_DEFAULT.metrics with screen_width/screen_height swapped for a
     * portrait-mounted panel (320x480, see platform::DisplayOrientation).
     * The composition root (src/main.cpp) assigns this into
     * runtime_theme().metrics once at boot, matching whichever orientation
     * core::Settings::orientation says is active — gui/theme can't depend
     * on gui/screens (where LayoutOrientation lives), so the selection
     * itself stays in main.cpp. */
    extern const Metrics METRICS_PORTRAIT;

    /** User-selectable accent-color presets (ThemePickerScreen), packed
     * 0xRRGGBB — plain hex rather than lv_color_t so this list, and the
     * uint32_t core::Settings::accent_color persisted from it, both stay
     * independent of any particular color type's in-memory layout. */
    constexpr uint8_t     ACCENT_PRESET_COUNT = 9;
    extern const uint32_t ACCENT_PRESETS[ACCENT_PRESET_COUNT];

    /** Glyphs from Typography::icon_day_night (Font Awesome Free Solid's
     * fa-sun/fa-moon, U+F185/U+F186) — plain UTF-8 string literals, the
     * same way LVGL's own LV_SYMBOL_* macros work, since these two icons
     * aren't part of that built-in set. */
    constexpr const char *ICON_SUN  = "\xEF\x86\x85";
    constexpr const char *ICON_MOON = "\xEF\x86\x86";

} // namespace gui
