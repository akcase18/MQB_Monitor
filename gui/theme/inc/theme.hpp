/**
 * @file theme.hpp
 * @brief Palette, typography, and metrics tokens for gui/widgets and
 * gui/screens. THEME_DEFAULT is the one reskin lever — a second theme
 * variant is just another `const Theme` with different values, touching
 * nothing outside gui/theme.
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
    };

    /**
     * symbol_lg must point at an LVGL built-in font (e.g.
     * lv_font_montserrat_32) — LV_SYMBOL_ glyphs live in Unicode's private-use
     * area and aren't present in the custom vw and digital_display fonts,
     * which were generated from a text-only TTF.
     */
    struct Typography
    {
            const lv_font_t *data_sm;   /* small-panel data readout */
            const lv_font_t *data_lg;   /* large-panel data readout */
            const lv_font_t *label_sm;  /* small-panel name label */
            const lv_font_t *label_lg;  /* large-panel name label */
            const lv_font_t *ui;        /* general UI text (screens, buttons) */
            const lv_font_t *symbol_lg; /* done/settings buttons */
    };

    struct Metrics
    {
            uint16_t screen_width;
            uint16_t screen_height;
            uint8_t  panel_radius;
            uint8_t  panel_gap;
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

} // namespace gui
