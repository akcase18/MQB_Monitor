#include "panel_widget.hpp"

#include <stdio.h>

namespace gui
{

    void PanelWidget::init(lv_obj_t *parent, const Theme &theme)
    {
        theme_ = &theme;

        btn_ = lv_btn_create(parent);
        lv_obj_remove_style_all(btn_);
        /* Panels cover nearly the whole screen, so without this a swipe
         * gesture starting on one (i.e. almost anywhere) would never reach
         * the screen's own gesture handler — bubbling is opt-in per object,
         * not a default. */
        lv_obj_set_gesture_bubble(btn_, true);
        lv_obj_set_style_bg_color(btn_, theme.palette.bg, 0);
        lv_obj_set_style_bg_opa(btn_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_, theme.palette.fg, 0);
        lv_obj_set_style_radius(btn_, theme.metrics.panel_radius, 0);
        lv_obj_set_style_shadow_width(btn_, 0, 0);
        lv_obj_set_style_pad_bottom(btn_, 2, 0);
        lv_obj_set_style_bg_color(btn_, theme.palette.accent, LV_STATE_PRESSED);

        data_ = lv_label_create(btn_);
        lv_obj_set_style_text_color(data_, theme.palette.fg, 0);
        lv_obj_set_style_bg_opa(data_, LV_OPA_TRANSP, 0);

        label_ = lv_label_create(btn_);
        lv_obj_set_style_text_color(label_, theme.palette.fg, 0);
        lv_obj_set_width(label_, LV_SIZE_CONTENT);
        lv_obj_set_align(label_, LV_ALIGN_BOTTOM_MID);
    }

    void PanelWidget::set_view_model(const PanelViewModel &vm)
    {
        const Typography &type = theme_->typography;

        lv_obj_set_style_text_font(
            data_, vm.large ? type.data_lg : type.data_sm, 0);
        lv_obj_set_style_translate_y(data_, -10, 0);
        lv_label_set_text(data_, vm.value_str);
        lv_obj_center(data_);

        lv_obj_set_style_text_font(
            label_, vm.large ? type.label_lg : type.label_sm, 0);
        if (vm.unit_label[0] != '\0')
        {
            char combined[PANEL_NAME_LEN + PANEL_UNIT_LABEL_LEN + 2];
            snprintf(
                combined, sizeof(combined), "%s %s", vm.name, vm.unit_label);
            lv_label_set_text(label_, combined);
        }
        else
        {
            lv_label_set_text(label_, vm.name);
        }

        if (vm.warn_hi_active)
        {
            lv_obj_set_style_bg_color(data_, theme_->palette.warn_hi, 0);
            lv_obj_set_style_bg_opa(data_, LV_OPA_COVER, 0);
        }
        else if (vm.warn_lo_active)
        {
            lv_obj_set_style_bg_color(data_, theme_->palette.warn_lo, 0);
            lv_obj_set_style_bg_opa(data_, LV_OPA_COVER, 0);
        }
        else
        {
            lv_obj_set_style_bg_opa(data_, LV_OPA_TRANSP, 0);
        }
    }

    void PanelWidget::set_visible(bool visible)
    {
        lv_obj_set_hidden(btn_, !visible);
    }

} // namespace gui
