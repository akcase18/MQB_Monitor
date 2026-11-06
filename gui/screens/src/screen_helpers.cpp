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

        /**
         * Every screen here is a fixed, fully custom layout on a fixed-size
         * panel — nothing should ever scroll. Hiding the scrollbar alone
         * (LV_SCROLLBAR_MODE_OFF) only hides the visual indicator; the object
         * stays scrollable and can still eat swipes as scroll attempts,
         * which is what was making some touches register as taps on the
         * wrong element.
         */
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
        /**
         * Touchable 10px past the drawn 40x40 on every side, so it's
         * easier to hit and looks the same. That reaches down to y=55,
         * short of the first tappable thing on any screen (y=60,
         * Logging's toggle row).
         */
        lv_obj_set_ext_click_area(close.btn, 10);

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
        /**
         * lv_btn's default theme pads its content in from the button's own
         * edges — without zeroing that, LEFT_MID-aligned children sit
         * further right than the plain labels elsewhere on the same
         * screen, which aren't buttons and so have no such padding.
         */
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

    namespace
    {
        /**
         * Cancel/Confirm button height in a confirm popup -- taller than
         * lv_msgbox's cramped ~43px default footer, same rationale as the
         * screens' own control rows wanting an easy tap target.
         */
        constexpr lv_coord_t CONFIRM_POPUP_FOOTER_HEIGHT = 60;

        /**
         * Matches the flat, bordered look of the screens' own buttons, so
         * the footer doesn't end up in LVGL's default light-mode styling.
         * `fill` (the Confirm button) sits solid at rest in the color that
         * every other button here only shows on press -- marking the one
         * that actually commits, matching the button matrix's own
         * checked-state treatment (style_flat_matrix() above).
         */
        FLASHMEM void style_flat_footer_button(lv_obj_t    *btn,
                                               const Theme &theme,
                                               const lv_color_t *fill
                                               = nullptr)
        {
            lv_obj_set_style_bg_color(
                btn, fill != nullptr ? *fill : theme.palette.bg, 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(btn, 8, 0);
            lv_obj_set_style_border_width(
                btn, theme.metrics.panel_border_width, 0);
            lv_obj_set_style_border_color(btn, theme.palette.panel_border, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_text_color(btn, theme.palette.fg, 0);
            lv_obj_set_style_bg_color(
                btn,
                fill != nullptr ? *fill : theme.palette.accent,
                LV_STATE_PRESSED);
        }
    } // namespace

    FLASHMEM ConfirmPopup create_confirm_popup(const Theme &theme,
                                               const char  *title_text,
                                               const char  *body_text,
                                               const char  *confirm_text,
                                               bool         destructive)
    {
        ConfirmPopup popup {};

        popup.mbox = lv_msgbox_create(nullptr);
        lv_obj_set_style_bg_color(popup.mbox, theme.palette.bg, 0);
        lv_obj_set_style_bg_opa(popup.mbox, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(
            popup.mbox, theme.metrics.panel_border_width, 0);
        lv_obj_set_style_border_color(
            popup.mbox, theme.palette.panel_border, 0);
        lv_obj_set_style_radius(popup.mbox, 10, 0);

        /**
         * Both strings stay short and single-line on purpose: a full
         * sentence of each wrapped onto several lines apiece, which read
         * as cluttered at the popup's ~260px width in both orientations.
         */
        lv_obj_t *title = lv_msgbox_add_title(popup.mbox, title_text);
        lv_obj_set_style_text_color(title, theme.palette.fg, 0);
        lv_obj_set_style_text_font(title, theme.typography.label_lg, 0);

        /**
         * lv_msgbox_add_title() is what actually creates the header
         * container (get_header() would return null before this point) --
         * it picks up LVGL's default (light-mode) theme styling on its own
         * bg, so override it explicitly to match everything else here
         * being dark.
         */
        lv_obj_t *header = lv_msgbox_get_header(popup.mbox);
        lv_obj_set_style_bg_color(header, theme.palette.bg, 0);
        lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);

        /**
         * No body at all for a null one -- lv_label_set_text(nullptr)
         * would keep the new label's placeholder ("Text").
         */
        if (body_text != nullptr)
        {
            lv_obj_t *body = lv_msgbox_add_text(popup.mbox, body_text);
            lv_obj_set_style_text_color(body, theme.palette.fg, 0);
            lv_obj_set_style_text_font(body, theme.typography.label_lg, 0);
        }

        /**
         * Cancel added first, Confirm second -- lv_msgbox's footer is a
         * left-to-right flex row, so this puts Confirm on the right.
         */
        popup.cancel_btn = lv_msgbox_add_footer_button(popup.mbox, "Cancel");
        popup.confirm_btn
            = lv_msgbox_add_footer_button(popup.mbox, confirm_text);

        lv_color_t fill
            = destructive ? theme.palette.warn_hi : theme.palette.accent;
        style_flat_footer_button(popup.cancel_btn, theme);
        style_flat_footer_button(popup.confirm_btn, theme, &fill);

        /**
         * lv_msgbox sizes footer buttons to their labels, so "Cancel" sat
         * wider than "Start". A shared percentage evens them out and the
         * footer's SPACE_EVENLY flex keeps the gaps equal.
         */
        lv_obj_set_width(popup.cancel_btn, LV_PCT(40));
        lv_obj_set_width(popup.confirm_btn, LV_PCT(40));

        /**
         * The buttons are LV_PCT(100) of the footer, so growing the footer
         * grows them too.
         */
        lv_obj_set_height(lv_msgbox_get_footer(popup.mbox),
                          CONFIRM_POPUP_FOOTER_HEIGHT);

        return popup;
    }

    FLASHMEM lv_obj_t *add_popup_countdown(const ConfirmPopup &popup,
                                           const Theme        &theme,
                                           int32_t             range)
    {
        constexpr lv_coord_t SIZE  = 24;
        constexpr lv_coord_t WIDTH = 3;

        /**
         * The header is a flex row with the title growing to fill it, so
         * appending here lands the ring flush right of the title.
         */
        lv_obj_t *ring = lv_arc_create(lv_msgbox_get_header(popup.mbox));
        lv_obj_set_size(ring, SIZE, SIZE);
        lv_obj_set_clickable(ring, false);
        lv_obj_remove_style(ring, nullptr, LV_PART_KNOB | LV_STATE_ANY);

        /**
         * A full circle from 12 o'clock, draining clockwise.
         */
        lv_arc_set_rotation(ring, 270);
        lv_arc_set_bg_angles(ring, 0, 360);
        lv_arc_set_mode(ring, LV_ARC_MODE_REVERSE);
        lv_arc_set_range(ring, 0, range);
        lv_arc_set_value(ring, range);

        /**
         * No track: the drained part just goes back to the popup's own
         * background, so an empty ring is no ring at all.
         */
        lv_obj_set_style_arc_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(ring, WIDTH, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(
            ring, theme.palette.accent, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(ring, false, LV_PART_INDICATOR);
        lv_obj_set_style_pad_all(ring, 0, LV_PART_MAIN);

        return ring;
    }

    FLASHMEM lv_obj_t *create_status_tile(lv_obj_t    *parent,
                                          const Theme &theme,
                                          const char  *caption,
                                          lv_coord_t   x,
                                          lv_coord_t   y,
                                          lv_coord_t   width,
                                          lv_coord_t   height)
    {
        lv_obj_t *tile = lv_obj_create(parent);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, width, height);
        lv_obj_align(tile, LV_ALIGN_TOP_LEFT, x, y);

        lv_obj_t *caption_lbl = lv_label_create(tile);
        lv_label_set_text(caption_lbl, caption);
        lv_obj_set_style_text_font(caption_lbl, theme.typography.label_lg, 0);
        lv_obj_set_style_text_color(
            caption_lbl, theme.palette.panel_border, 0);
        lv_obj_align(caption_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *value_lbl = lv_label_create(tile);
        lv_label_set_text(value_lbl, "--");
        lv_obj_set_style_text_font(value_lbl, theme.typography.ui, 0);
        lv_obj_set_style_text_color(value_lbl, theme.palette.fg, 0);
        lv_obj_align(value_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        return value_lbl;
    }

} // namespace gui
