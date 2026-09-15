#include "toast_overlay.hpp"

#include "mem_placement.hpp"

namespace gui
{

    namespace
    {
        /**
         * Gap between the overlay and the bottom edge of the panel. Small:
         * on a 172px strip there is nothing to spare, and the overlay is
         * gone again in two seconds. */
        constexpr lv_coord_t TOAST_MARGIN  = 6;
        constexpr lv_coord_t TOAST_PAD_X   = 10;
        constexpr lv_coord_t TOAST_PAD_Y   = 6;
        constexpr lv_coord_t TOAST_RADIUS  = 6;
        constexpr lv_coord_t TOAST_BORDER  = 2;
        /**
         * Leaves the border and a little air either side of the widest
         * line the overlay can hold before LVGL starts wrapping it. */
        constexpr lv_coord_t TOAST_SIDE_INSET = 2 * TOAST_MARGIN;
    } // namespace

    FLASHMEM void ToastOverlay::init(const Theme &theme)
    {
        if (box_ != nullptr)
        {
            lv_obj_delete(box_);
            box_ = nullptr;
        }
        if (timer_ != nullptr)
        {
            lv_timer_delete(timer_);
            timer_ = nullptr;
        }

        box_ = lv_obj_create(lv_layer_top());

        /**
         * Never a control. Without this the overlay would eat presses
         * aimed at whatever it happens to be covering -- which on a
         * touchscreen product is a panel, and on this one is nothing, but
         * the widget should be correct on both. */
        lv_obj_remove_flag(box_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_size(box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(
            box_, theme.metrics.screen_width - TOAST_SIDE_INSET, 0);
        lv_obj_set_style_radius(box_, TOAST_RADIUS, 0);
        lv_obj_set_style_pad_hor(box_, TOAST_PAD_X, 0);
        lv_obj_set_style_pad_ver(box_, TOAST_PAD_Y, 0);
        lv_obj_set_style_bg_color(box_, theme.palette.bg, 0);
        lv_obj_set_style_bg_opa(box_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(box_, TOAST_BORDER, 0);
        lv_obj_set_style_border_color(box_, theme.palette.accent, 0);

        label_ = lv_label_create(box_);
        lv_obj_set_style_text_color(label_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(label_, theme.typography.ui, 0);
        lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label_, "");

        /**
         * Bottom-centre: the value readouts are vertically centred in
         * their panels, and the name labels sit at the top, so this is
         * the band that covers the least. */
        lv_obj_align(box_, LV_ALIGN_BOTTOM_MID, 0, -TOAST_MARGIN);

        lv_obj_add_flag(box_, LV_OBJ_FLAG_HIDDEN);

        /**
         * One timer for the widget's whole life, paused when nothing is
         * showing, rather than a fresh one-shot per message. A toast is
         * raised on every button press; creating and deleting an lv_timer
         * that often is churn, and a self-deleting timer leaves a pointer
         * here that has to be remembered not to touch. */
        timer_ = lv_timer_create(timeout_cb, TOAST_DEFAULT_MS, this);
        lv_timer_pause(timer_);
    }

    void ToastOverlay::show(const char *text, uint32_t duration_ms)
    {
        if (box_ == nullptr || text == nullptr)
        {
            return;
        }

        lv_label_set_text(label_, text);

        /**
         * Re-aligned after the text changes: the box is LV_SIZE_CONTENT,
         * so a taller or wider message moves where its bottom-centre
         * anchor puts it. */
        lv_obj_align(box_, LV_ALIGN_BOTTOM_MID, 0, -TOAST_MARGIN);
        lv_obj_remove_flag(box_, LV_OBJ_FLAG_HIDDEN);

        /**
         * Raised above anything else that has been put on the top layer
         * since -- there is only one overlay today, but "the newest
         * message is the visible one" should not depend on that. */
        lv_obj_move_foreground(box_);

        if (duration_ms == 0)
        {
            lv_timer_pause(timer_);
            return;
        }

        lv_timer_set_period(timer_, duration_ms);
        lv_timer_reset(timer_);
        lv_timer_resume(timer_);
    }

    void ToastOverlay::hide()
    {
        if (box_ == nullptr)
        {
            return;
        }

        lv_timer_pause(timer_);
        lv_obj_add_flag(box_, LV_OBJ_FLAG_HIDDEN);
    }

    bool ToastOverlay::visible() const
    {
        return box_ != nullptr && !lv_obj_has_flag(box_, LV_OBJ_FLAG_HIDDEN);
    }

    void ToastOverlay::timeout_cb(lv_timer_t *timer)
    {
        auto *self = static_cast<ToastOverlay *>(lv_timer_get_user_data(timer));
        self->hide();
    }

} // namespace gui
