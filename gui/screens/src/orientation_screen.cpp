#include "orientation_screen.hpp"
#include "mem_placement.hpp"

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        /* Same rhythm as SettingsScreen: Landscape puts each label beside
         * its control on one row; Portrait gives each label a line of its
         * own above a full-width control. */
        constexpr lv_coord_t HEADER_DIVIDER_Y = 50;
        constexpr lv_coord_t ROW_HEIGHT       = 46;
        constexpr lv_coord_t SCREEN_MARGIN    = 20;
        constexpr lv_coord_t LABEL_GAP        = 16;

        constexpr lv_coord_t LAYOUT_ROW_Y = 70;
        constexpr lv_coord_t DIVIDER1_Y   = 136;
        constexpr lv_coord_t ROTATE_ROW_Y = 156;

        constexpr lv_coord_t PORTRAIT_LAYOUT_LABEL_Y = 68;
        constexpr lv_coord_t PORTRAIT_LAYOUT_ROW_Y   = 94;
        constexpr lv_coord_t PORTRAIT_DIVIDER1_Y     = 160;
        constexpr lv_coord_t PORTRAIT_ROTATE_LABEL_Y = 176;
        constexpr lv_coord_t PORTRAIT_ROTATE_ROW_Y   = 202;

        /* Word labels, so explicit item padding like ConfiguratorScreen's
         * Units/Mode matrices rather than LVGL's default. */
        constexpr lv_coord_t MATRIX_ITEM_PAD = 8;

        /* Indices into layout_map below. */
        constexpr uint32_t BTN_HORIZONTAL = 0;
        constexpr uint32_t BTN_VERTICAL   = 1;

        FLASHMEM lv_obj_t *create_row_label(lv_obj_t    *parent,
                                            const Theme &theme,
                                            const char  *text)
        {
            lv_obj_t *label = lv_label_create(parent);
            lv_label_set_text(label, text);
            lv_obj_set_style_text_color(label, theme.palette.fg, 0);
            lv_obj_set_style_text_font(label, theme.typography.label_lg, 0);
            return label;
        }
    } // namespace

    FLASHMEM void OrientationScreen::init(const Theme          &theme,
                                          LayoutConfirmCallback layout_confirm_cb,
                                          void                 *layout_confirm_ctx,
                                          RotateCallback        rotate_cb,
                                          void                 *rotate_ctx)
    {
        theme_              = &theme;
        layout_confirm_cb_  = layout_confirm_cb;
        layout_confirm_ctx_ = layout_confirm_ctx;
        rotate_cb_          = rotate_cb;
        rotate_ctx_         = rotate_ctx;

        screen_ = create_screen(theme);

        bool portrait = theme.metrics.screen_height > theme.metrics.screen_width;

        create_nav_title(screen_, theme, "Orientation");

        DoneButton close = create_close_button(screen_, theme);
        lv_obj_add_event_cb(
            close.btn, close_cb_handler, LV_EVENT_CLICKED, this);

        create_divider(screen_, theme, HEADER_DIVIDER_Y);

        lv_coord_t row_width = theme.metrics.screen_width - 2 * SCREEN_MARGIN;

        lv_obj_t *layout_label = create_row_label(screen_, theme, "Layout");
        lv_obj_t *rotate_label = create_row_label(screen_, theme, "Rotate");

        /* Landscape: both controls start just past the wider of the two
         * labels (measured, not guessed -- see SettingsScreen's
         * brightness label), so they line up as one column. */
        lv_coord_t control_x = 0;
        if (!portrait)
        {
            lv_obj_update_layout(layout_label);
            lv_obj_update_layout(rotate_label);
            lv_coord_t label_width = lv_obj_get_width(layout_label);
            if (lv_obj_get_width(rotate_label) > label_width)
            {
                label_width = lv_obj_get_width(rotate_label);
            }
            control_x = label_width + LABEL_GAP;
        }
        lv_coord_t control_width = row_width - control_x;

        lv_coord_t layout_row_y
            = portrait ? PORTRAIT_LAYOUT_ROW_Y : LAYOUT_ROW_Y;
        lv_coord_t rotate_row_y
            = portrait ? PORTRAIT_ROTATE_ROW_Y : ROTATE_ROW_Y;

        if (portrait)
        {
            lv_obj_align(layout_label,
                         LV_ALIGN_TOP_LEFT,
                         SCREEN_MARGIN,
                         PORTRAIT_LAYOUT_LABEL_Y);
            lv_obj_align(rotate_label,
                         LV_ALIGN_TOP_LEFT,
                         SCREEN_MARGIN,
                         PORTRAIT_ROTATE_LABEL_Y);
        }
        else
        {
            /* Vertically centred on its control's row. */
            lv_obj_align(layout_label,
                         LV_ALIGN_TOP_LEFT,
                         SCREEN_MARGIN,
                         layout_row_y
                             + (ROW_HEIGHT - lv_obj_get_height(layout_label))
                                   / 2);
            lv_obj_align(rotate_label,
                         LV_ALIGN_TOP_LEFT,
                         SCREEN_MARGIN,
                         rotate_row_y
                             + (ROW_HEIGHT - lv_obj_get_height(rotate_label))
                                   / 2);
        }

        static const char *const layout_map[] = { "Horizontal", "Vertical", "" };
        layout_matrix_ = lv_buttonmatrix_create(screen_);
        /* Portrait's half-width cells are too narrow for "Horizontal" in
         * the full UI font. */
        lv_obj_set_style_text_font(
            layout_matrix_,
            portrait ? theme.typography.label_lg : theme.typography.ui,
            0);
        lv_obj_set_size(layout_matrix_, control_width, ROW_HEIGHT);
        lv_obj_align(layout_matrix_,
                     LV_ALIGN_TOP_LEFT,
                     SCREEN_MARGIN + control_x,
                     layout_row_y);
        lv_buttonmatrix_set_map(layout_matrix_, layout_map);
        lv_buttonmatrix_set_one_checked(layout_matrix_, true);
        /* CLICK_TRIG moves VALUE_CHANGED from press to release. Without
         * it the event fires before the matrix toggles its checked state
         * (that happens on release), so the handler saw no change, and
         * the release then flipped the check with no popup. Firing on
         * release also keeps the modal popup from appearing under a
         * finger that's still down. */
        lv_buttonmatrix_set_button_ctrl_all(
            layout_matrix_,
            static_cast<lv_buttonmatrix_ctrl_t>(
                LV_BUTTONMATRIX_CTRL_CHECKABLE
                | LV_BUTTONMATRIX_CTRL_CLICK_TRIG));
        style_flat_matrix(layout_matrix_, theme, MATRIX_ITEM_PAD);
        lv_obj_add_event_cb(layout_matrix_,
                            layout_changed_cb,
                            LV_EVENT_VALUE_CHANGED,
                            this);

        create_divider(
            screen_, theme, portrait ? PORTRAIT_DIVIDER1_Y : DIVIDER1_Y);

        /* Styled like SettingsScreen's Day/Night toggle -- a plain bordered
         * button, accent only while pressed. Not checkable: it's an
         * action, not a state, and the screen turning over under the
         * user's finger already shows it worked. */
        lv_obj_t *rotate_btn = lv_btn_create(screen_);
        lv_obj_set_size(rotate_btn, control_width, ROW_HEIGHT);
        lv_obj_align(rotate_btn,
                     LV_ALIGN_TOP_LEFT,
                     SCREEN_MARGIN + control_x,
                     rotate_row_y);
        lv_obj_set_style_bg_color(rotate_btn, theme.palette.bg, 0);
        lv_obj_set_style_bg_opa(rotate_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(rotate_btn, 8, 0);
        lv_obj_set_style_border_width(
            rotate_btn, theme.metrics.panel_border_width, 0);
        lv_obj_set_style_border_color(
            rotate_btn, theme.palette.panel_border, 0);
        lv_obj_set_style_shadow_width(rotate_btn, 0, 0);
        lv_obj_set_style_bg_color(
            rotate_btn, theme.palette.accent, LV_STATE_PRESSED);
        lv_obj_add_event_cb(
            rotate_btn, rotate_clicked_cb, LV_EVENT_CLICKED, this);

        lv_obj_t *rotate_btn_lbl = lv_label_create(rotate_btn);
        lv_label_set_text(rotate_btn_lbl, "180\xC2\xB0");
        lv_obj_set_style_text_font(rotate_btn_lbl, theme.typography.ui, 0);
        lv_obj_set_style_text_color(rotate_btn_lbl, theme.palette.fg, 0);
        lv_obj_center(rotate_btn_lbl);
    }

    FLASHMEM void OrientationScreen::open(LayoutOrientation orientation,
                                 lv_obj_t         *return_screen)
    {
        orientation_   = orientation;
        return_screen_ = return_screen;
        sync_layout_matrix();
        lv_screen_load(screen_);
    }

    FLASHMEM void OrientationScreen::sync_layout_matrix()
    {
        lv_buttonmatrix_set_button_ctrl(
            layout_matrix_,
            orientation_ == LayoutOrientation::Vertical ? BTN_VERTICAL
                                                        : BTN_HORIZONTAL,
            LV_BUTTONMATRIX_CTRL_CHECKED);
    }

    FLASHMEM void OrientationScreen::layout_changed_cb(lv_event_t *e)
    {
        auto *self
            = static_cast<OrientationScreen *>(lv_event_get_user_data(e));

        /* Which button was tapped, from the event itself -- not the
         * checked state, which one_checked has already moved to it. */
        uint32_t tapped = *static_cast<uint32_t *>(lv_event_get_param(e));
        LayoutOrientation picked = tapped == BTN_VERTICAL
                                       ? LayoutOrientation::Vertical
                                       : LayoutOrientation::Horizontal;
        if (picked == self->orientation_)
        {
            return;
        }

        /* Put the check back until the popup is answered -- the matrix
         * shouldn't claim a layout that a Cancel is about to throw away. */
        self->sync_layout_matrix();
        self->pending_orientation_ = picked;

        ConfirmPopup popup = create_confirm_popup(
            *self->theme_,
            picked == LayoutOrientation::Vertical ? "Switch to Vertical?"
                                                  : "Switch to Horizontal?",
            "Display will restart.",
            "Restart");

        lv_obj_add_event_cb(
            popup.confirm_btn, layout_confirm_cb, LV_EVENT_CLICKED, self);
        lv_obj_add_event_cb(
            popup.cancel_btn, layout_cancel_cb, LV_EVENT_CLICKED, self);

        self->confirm_mbox_ = popup.mbox;
    }

    FLASHMEM void OrientationScreen::layout_confirm_cb(lv_event_t *e)
    {
        auto *self
            = static_cast<OrientationScreen *>(lv_event_get_user_data(e));
        lv_msgbox_close_async(self->confirm_mbox_);
        self->confirm_mbox_ = nullptr;

        if (self->layout_confirm_cb_ != nullptr)
        {
            self->layout_confirm_cb_(self->layout_confirm_ctx_,
                                     self->pending_orientation_);
        }
    }

    FLASHMEM void OrientationScreen::layout_cancel_cb(lv_event_t *e)
    {
        auto *self
            = static_cast<OrientationScreen *>(lv_event_get_user_data(e));
        lv_msgbox_close_async(self->confirm_mbox_);
        self->confirm_mbox_ = nullptr;
    }

    FLASHMEM void OrientationScreen::rotate_clicked_cb(lv_event_t *e)
    {
        auto *self
            = static_cast<OrientationScreen *>(lv_event_get_user_data(e));
        if (self->rotate_cb_ != nullptr)
        {
            self->rotate_cb_(self->rotate_ctx_);
        }
    }

    FLASHMEM void OrientationScreen::close_cb_handler(lv_event_t *e)
    {
        auto *self
            = static_cast<OrientationScreen *>(lv_event_get_user_data(e));
        if (self->return_screen_ != nullptr)
        {
            lv_screen_load(self->return_screen_);
        }
    }

} // namespace gui
