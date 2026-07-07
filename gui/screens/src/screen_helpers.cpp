#include "screen_helpers.hpp"

namespace gui
{

    lv_obj_t *create_screen(const Theme &theme)
    {
        lv_obj_t *screen = lv_obj_create(nullptr);
        lv_obj_set_size(
            screen, theme.metrics.screen_width, theme.metrics.screen_height);
        lv_obj_set_style_bg_color(screen, theme.palette.bg, 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(screen, 0, 0);
        lv_obj_set_style_radius(screen, 0, 0);

        /* Every screen here is a fixed, fully custom layout on a fixed-size
         * panel — nothing should ever scroll. Hiding the scrollbar alone
         * (LV_SCROLLBAR_MODE_OFF) only hides the visual indicator; the object
         * stays scrollable and can still eat swipes as scroll attempts,
         * which is what was making some touches register as taps on the
         * wrong element. */
        lv_obj_set_scrollable(screen, false);

        return screen;
    }

    DoneButton create_done_button(lv_obj_t *parent, const Theme &theme)
    {
        DoneButton done {};

        done.btn = lv_btn_create(parent);
        lv_obj_set_style_bg_color(done.btn, theme.palette.bg, 0);
        lv_obj_set_style_text_color(done.btn, theme.palette.fg, 0);
        lv_obj_set_style_radius(done.btn, 5, 0);
        lv_obj_set_style_bg_color(
            done.btn, theme.palette.accent, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(done.btn, 0, 0);
        lv_obj_align(done.btn, LV_ALIGN_BOTTOM_RIGHT, -20, -20);

        done.label = lv_label_create(done.btn);
        lv_obj_set_style_text_font(done.label, theme.typography.symbol_lg, 0);
        lv_label_set_text(done.label, LV_SYMBOL_OK);
        lv_obj_center(done.label);

        return done;
    }

    DoneButton create_close_button(lv_obj_t *parent, const Theme &theme)
    {
        DoneButton close {};

        close.btn = lv_btn_create(parent);
        lv_obj_set_size(close.btn, 32, 32);
        lv_obj_set_style_bg_opa(close.btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(close.btn, 0, 0);
        lv_obj_set_style_shadow_width(close.btn, 0, 0);
        lv_obj_set_style_bg_color(
            close.btn, theme.palette.accent, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(close.btn, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_align(close.btn, LV_ALIGN_TOP_RIGHT, -16, 8);

        close.label = lv_label_create(close.btn);
        lv_obj_set_style_text_font(close.label, theme.typography.symbol_lg, 0);
        lv_label_set_text(close.label, LV_SYMBOL_CLOSE);
        lv_obj_center(close.label);

        return close;
    }

    lv_obj_t *create_divider(lv_obj_t *parent, const Theme &theme, lv_coord_t y)
    {
        lv_obj_t *div = lv_obj_create(parent);
        lv_obj_remove_style_all(div);
        lv_obj_set_size(div, theme.metrics.screen_width, 2);
        lv_obj_set_style_bg_color(div, theme.palette.panel_border, 0);
        lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
        lv_obj_align(div, LV_ALIGN_TOP_MID, 0, y);
        return div;
    }

} // namespace gui
