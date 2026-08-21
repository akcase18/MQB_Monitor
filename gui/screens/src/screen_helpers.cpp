#include "screen_helpers.hpp"
#include "mem_placement.hpp"

namespace gui
{

    FLASHMEM lv_obj_t *create_screen(const Theme &theme)
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

    FLASHMEM DoneButton create_close_button(lv_obj_t    *parent,
                                           const Theme &theme)
    {
        DoneButton close {};

        close.btn = lv_btn_create(parent);
        lv_obj_set_size(close.btn, 40, 40);
        lv_obj_set_style_bg_opa(close.btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(close.btn, 0, 0);
        lv_obj_set_style_shadow_width(close.btn, 0, 0);
        lv_obj_set_style_bg_color(
            close.btn, theme.palette.accent, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(close.btn, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_align(close.btn, LV_ALIGN_TOP_RIGHT, -16, 6);

        close.label = lv_label_create(close.btn);
        lv_obj_set_style_text_font(close.label, theme.typography.symbol_lg, 0);
        lv_label_set_text(close.label, LV_SYMBOL_CLOSE);
        lv_obj_center(close.label);

        return close;
    }

    FLASHMEM lv_obj_t *create_divider(lv_obj_t    *parent,
                                      const Theme &theme,
                                      lv_coord_t   y)
    {
        lv_obj_t *div = lv_obj_create(parent);
        lv_obj_remove_style_all(div);
        lv_obj_set_size(div, theme.metrics.screen_width, 2);
        lv_obj_set_style_bg_color(div, theme.palette.panel_border, 0);
        lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
        lv_obj_align(div, LV_ALIGN_TOP_MID, 0, y);
        return div;
    }

    FLASHMEM lv_obj_t *create_nav_title(lv_obj_t    *parent,
                                        const Theme &theme,
                                        const char  *text)
    {
        lv_obj_t *title = lv_label_create(parent);
        if (text != nullptr)
        {
            lv_label_set_text(title, text);
        }
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 12);
        lv_obj_set_style_text_color(title, theme.palette.fg, 0);
        lv_obj_set_style_text_font(title, theme.typography.title, 0);
        return title;
    }

    FLASHMEM ListRow create_list_row(lv_obj_t    *parent,
                                     const Theme &theme,
                                     const char  *label_text)
    {
        ListRow row {};

        row.btn = lv_btn_create(parent);
        lv_obj_set_style_bg_opa(row.btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row.btn, 0, 0);
        lv_obj_set_style_shadow_width(row.btn, 0, 0);
        /* lv_btn's default theme pads its content in from the button's own
         * edges — without zeroing that, LEFT_MID-aligned children sit
         * further right than the plain labels elsewhere on the same
         * screen, which aren't buttons and so have no such padding. */
        lv_obj_set_style_pad_all(row.btn, 0, 0);
        lv_obj_set_style_bg_color(
            row.btn, theme.palette.accent, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row.btn, LV_OPA_30, LV_STATE_PRESSED);

        row.label = lv_label_create(row.btn);
        lv_label_set_text(row.label, label_text);
        lv_obj_set_style_text_color(row.label, theme.palette.fg, 0);
        lv_obj_set_style_text_font(row.label, theme.typography.label_lg, 0);
        lv_obj_align(row.label, LV_ALIGN_LEFT_MID, 0, 0);

        row.chevron = lv_label_create(row.btn);
        lv_obj_set_style_text_font(row.chevron, theme.typography.symbol_lg, 0);
        lv_label_set_text(row.chevron, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(row.chevron, theme.palette.fg, 0);
        lv_obj_align(row.chevron, LV_ALIGN_RIGHT_MID, 0, 0);

        return row;
    }

    FLASHMEM void style_flat_matrix(lv_obj_t    *matrix,
                                    const Theme &theme,
                                    lv_coord_t   item_pad)
    {
        lv_obj_set_style_bg_opa(matrix, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(matrix, 0, 0);
        lv_obj_set_style_shadow_width(matrix, 0, 0);
        lv_obj_set_style_pad_all(matrix, 0, 0);
        lv_obj_set_style_pad_gap(matrix, 8, 0);

        lv_obj_set_style_bg_color(matrix, theme.palette.bg, LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(matrix, LV_OPA_COVER, LV_PART_ITEMS);
        lv_obj_set_style_text_color(matrix, theme.palette.fg, LV_PART_ITEMS);
        if (item_pad != MATRIX_ITEM_PAD_KEEP)
        {
            lv_obj_set_style_pad_all(matrix, item_pad, LV_PART_ITEMS);
        }
        lv_obj_set_style_radius(matrix, 8, LV_PART_ITEMS);
        lv_obj_set_style_border_width(
            matrix, theme.metrics.panel_border_width, LV_PART_ITEMS);
        lv_obj_set_style_border_color(
            matrix, theme.palette.panel_border, LV_PART_ITEMS);
        lv_obj_set_style_shadow_width(matrix, 0, LV_PART_ITEMS);
        lv_obj_set_style_bg_color(
            matrix, theme.palette.accent, LV_PART_ITEMS | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(
            matrix, theme.palette.accent, LV_PART_ITEMS | LV_STATE_PRESSED);
    }

} // namespace gui
