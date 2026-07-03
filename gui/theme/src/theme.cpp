#include "theme.hpp"

extern "C"
{
    extern const lv_font_t digital_display_80_v2;
    extern const lv_font_t digital_display_115_v2;
    extern const lv_font_t vw_16;
    extern const lv_font_t vw_18;
    extern const lv_font_t vw_24;
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
            lv_palette_darken(LV_PALETTE_GREY, 3), /* accent */
            lv_palette_darken(LV_PALETTE_BLUE, 2), /* warn_lo */
            lv_palette_darken(LV_PALETTE_RED, 2),  /* warn_hi */
        },
        /* typography */
        {
            &digital_display_80_v2,
            &digital_display_115_v2,
            &vw_16,
            &vw_18,
            &vw_24,
            &lv_font_montserrat_32,
        },
        /* metrics */
        {
            480,
            320,
            10,
            6,
            8,
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
            lv_palette_darken(LV_PALETTE_BLUE_GREY, 4), /* bg */
            lv_color_white(),                           /* fg */
            lv_palette_main(LV_PALETTE_CYAN),           /* accent */
            lv_palette_main(LV_PALETTE_LIGHT_BLUE),     /* warn_lo */
            lv_palette_main(LV_PALETTE_ORANGE),         /* warn_hi */
        },
        /* typography */
        {
            &digital_display_80_v2,
            &digital_display_115_v2,
            &vw_16,
            &vw_18,
            &vw_24,
            &lv_font_montserrat_32,
        },
        /* metrics */
        {
            480,
            320,
            16,
            10,
            10,
            0.45f,
            0.55f,
        },
    };

} // namespace gui
