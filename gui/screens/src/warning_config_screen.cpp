#include "warning_config_screen.hpp"
#include "mem_placement.hpp"

#include <stdio.h>

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        constexpr lv_coord_t STEP_BTN_WIDTH  = 64;
        constexpr lv_coord_t STEP_BTN_HEIGHT = 56;
        constexpr lv_coord_t ROW_GAP         = 20;
        /**
         * Fixed size to prevent strange resizing with different values/units.
         */
        constexpr lv_coord_t VALUE_LBL_WIDTH = 170;

        constexpr lv_coord_t PORTRAIT_STEP_BTN_WIDTH = 56;
        constexpr lv_coord_t PORTRAIT_BUTTON_GAP     = 32;

        constexpr lv_coord_t HEADER_DIVIDER_Y = 50;
        constexpr lv_coord_t HI_LABEL_Y       = 64;
        constexpr lv_coord_t HI_SWITCH_Y      = 66;
        constexpr lv_coord_t HI_ROW_Y         = 102;

        constexpr lv_coord_t PORTRAIT_LABEL_Y   = 14;
        constexpr lv_coord_t PORTRAIT_SWITCH_Y  = 16;
        constexpr lv_coord_t PORTRAIT_VALUE_Y   = 60;
        constexpr lv_coord_t PORTRAIT_BUTTONS_Y = 112;

        FLASHMEM void style_step_button(lv_obj_t    *btn,
                                        const Theme &theme,
                                        lv_coord_t   width)
        {
            lv_obj_set_size(btn, width, STEP_BTN_HEIGHT);
            lv_obj_set_style_bg_color(btn, theme.palette.bg, 0);
            lv_obj_set_style_text_color(btn, theme.palette.fg, 0);
            lv_obj_set_style_radius(btn, 8, 0);
            lv_obj_set_style_border_width(
                btn, theme.metrics.panel_border_width, 0);
            lv_obj_set_style_border_color(btn, theme.palette.panel_border, 0);
            lv_obj_set_style_bg_color(
                btn, theme.palette.accent, LV_STATE_PRESSED);
            lv_obj_set_style_shadow_width(btn, 0, 0);
        }

        FLASHMEM lv_obj_t *create_control_row(lv_obj_t  *parent,
                                              lv_coord_t y_offset,
                                              lv_coord_t gap)
        {
            lv_obj_t *row = lv_obj_create(parent);
            lv_obj_remove_style_all(row);
            lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row,
                                  LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row, gap, 0);
            lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y_offset);
            return row;
        }
    } // namespace

    FLASHMEM void WarningConfigScreen::init(const Theme &theme,
                                            DoneCallback done_cb,
                                            void        *ctx)
    {
        done_cb_  = done_cb;
        done_ctx_ = ctx;

        screen_ = create_screen(theme);

        name_lbl_ = create_nav_title(screen_, theme, nullptr);

        DoneButton close = create_close_button(screen_, theme);
        close_btn_       = close.btn;
        lv_obj_add_event_cb(
            close_btn_, done_cb_handler, LV_EVENT_CLICKED, this);

        create_divider(screen_, theme, HEADER_DIVIDER_Y);

        bool portrait
            = theme.metrics.screen_height > theme.metrics.screen_width;
        lv_coord_t step_btn_width
            = portrait ? PORTRAIT_STEP_BTN_WIDTH : STEP_BTN_WIDTH;
        lv_coord_t row_gap = portrait ? PORTRAIT_BUTTON_GAP : ROW_GAP;

        lv_coord_t section_divider_y
            = (HEADER_DIVIDER_Y + theme.metrics.screen_height) / 2;

        hi_label_ = lv_label_create(screen_);
        lv_label_set_text(hi_label_, "High Warning");
        lv_obj_align(hi_label_,
                     LV_ALIGN_TOP_LEFT,
                     20,
                     portrait ? HEADER_DIVIDER_Y + PORTRAIT_LABEL_Y
                              : HI_LABEL_Y);
        lv_obj_set_style_text_color(hi_label_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(hi_label_, theme.typography.ui, 0);

        hi_sel_ = lv_switch_create(screen_);
        lv_obj_align(hi_sel_,
                     LV_ALIGN_TOP_RIGHT,
                     -20,
                     portrait ? HEADER_DIVIDER_Y + PORTRAIT_SWITCH_Y
                              : HI_SWITCH_Y);
        lv_obj_set_style_bg_color(hi_sel_,
                                  theme.palette.accent,
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(
            hi_sel_, hi_toggle_cb, LV_EVENT_VALUE_CHANGED, this);

        if (portrait)
        {
            hi_value_lbl_ = lv_label_create(screen_);
            lv_obj_set_style_text_color(hi_value_lbl_, theme.palette.fg, 0);
            lv_obj_set_style_text_font(hi_value_lbl_, theme.typography.ui, 0);
            lv_obj_align(hi_value_lbl_,
                         LV_ALIGN_TOP_MID,
                         0,
                         HEADER_DIVIDER_Y + PORTRAIT_VALUE_Y);
        }

        lv_obj_t *hi_row = create_control_row(
            screen_,
            portrait ? HEADER_DIVIDER_Y + PORTRAIT_BUTTONS_Y : HI_ROW_Y,
            row_gap);
        hi_minus_btn_ = lv_btn_create(hi_row);
        style_step_button(hi_minus_btn_, theme, step_btn_width);
        lv_obj_t *hi_minus_lbl = lv_label_create(hi_minus_btn_);
        lv_obj_set_style_text_font(hi_minus_lbl, theme.typography.symbol_lg, 0);
        lv_label_set_text(hi_minus_lbl, LV_SYMBOL_MINUS);
        lv_obj_center(hi_minus_lbl);

        if (!portrait)
        {
            hi_value_lbl_ = lv_label_create(hi_row);
            lv_obj_set_style_text_color(hi_value_lbl_, theme.palette.fg, 0);

            lv_obj_set_style_text_font(hi_value_lbl_, theme.typography.ui, 0);
            lv_obj_set_style_text_align(hi_value_lbl_, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(hi_value_lbl_, VALUE_LBL_WIDTH);
        }

        hi_plus_btn_ = lv_btn_create(hi_row);
        style_step_button(hi_plus_btn_, theme, step_btn_width);
        lv_obj_t *hi_plus_lbl = lv_label_create(hi_plus_btn_);
        lv_obj_set_style_text_font(hi_plus_lbl, theme.typography.symbol_lg, 0);
        lv_label_set_text(hi_plus_lbl, LV_SYMBOL_PLUS);
        lv_obj_center(hi_plus_lbl);

        create_divider(screen_, theme, section_divider_y);

        lo_label_ = lv_label_create(screen_);
        lv_label_set_text(lo_label_, "Low Warning");
        lv_obj_align(lo_label_,
                     LV_ALIGN_TOP_LEFT,
                     20,
                     section_divider_y + (portrait ? PORTRAIT_LABEL_Y : 14));
        lv_obj_set_style_text_color(lo_label_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(lo_label_, theme.typography.ui, 0);

        lo_sel_ = lv_switch_create(screen_);
        lv_obj_align(lo_sel_,
                     LV_ALIGN_TOP_RIGHT,
                     -20,
                     section_divider_y + (portrait ? PORTRAIT_SWITCH_Y : 16));
        lv_obj_set_style_bg_color(lo_sel_,
                                  theme.palette.accent,
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(
            lo_sel_, lo_toggle_cb, LV_EVENT_VALUE_CHANGED, this);

        if (portrait)
        {
            lo_value_lbl_ = lv_label_create(screen_);
            lv_obj_set_style_text_color(lo_value_lbl_, theme.palette.fg, 0);
            lv_obj_set_style_text_font(lo_value_lbl_, theme.typography.ui, 0);
            lv_obj_align(lo_value_lbl_,
                         LV_ALIGN_TOP_MID,
                         0,
                         section_divider_y + PORTRAIT_VALUE_Y);
        }

        lv_obj_t *lo_row = create_control_row(
            screen_,
            section_divider_y + (portrait ? PORTRAIT_BUTTONS_Y : 52),
            row_gap);
        lo_minus_btn_ = lv_btn_create(lo_row);
        style_step_button(lo_minus_btn_, theme, step_btn_width);
        lv_obj_t *lo_minus_lbl = lv_label_create(lo_minus_btn_);
        lv_obj_set_style_text_font(lo_minus_lbl, theme.typography.symbol_lg, 0);
        lv_label_set_text(lo_minus_lbl, LV_SYMBOL_MINUS);
        lv_obj_center(lo_minus_lbl);

        if (!portrait)
        {
            lo_value_lbl_ = lv_label_create(lo_row);
            lv_obj_set_style_text_color(lo_value_lbl_, theme.palette.fg, 0);
            lv_obj_set_style_text_font(lo_value_lbl_, theme.typography.ui, 0);
            lv_obj_set_style_text_align(lo_value_lbl_, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(lo_value_lbl_, VALUE_LBL_WIDTH);
        }

        lo_plus_btn_ = lv_btn_create(lo_row);
        style_step_button(lo_plus_btn_, theme, step_btn_width);
        lv_obj_t *lo_plus_lbl = lv_label_create(lo_plus_btn_);
        lv_obj_set_style_text_font(lo_plus_lbl, theme.typography.symbol_lg, 0);
        lv_label_set_text(lo_plus_lbl, LV_SYMBOL_PLUS);
        lv_obj_center(lo_plus_lbl);

        hi_minus_ctx_ = { this, true, -1.0f, false };
        hi_plus_ctx_  = { this, true, 1.0f, false };
        lo_minus_ctx_ = { this, false, -1.0f, false };
        lo_plus_ctx_  = { this, false, 1.0f, false };

        lv_obj_add_event_cb(
            hi_minus_btn_, step_button_cb, LV_EVENT_ALL, &hi_minus_ctx_);
        lv_obj_add_event_cb(
            hi_plus_btn_, step_button_cb, LV_EVENT_ALL, &hi_plus_ctx_);
        lv_obj_add_event_cb(
            lo_minus_btn_, step_button_cb, LV_EVENT_ALL, &lo_minus_ctx_);
        lv_obj_add_event_cb(
            lo_plus_btn_, step_button_cb, LV_EVENT_ALL, &lo_plus_ctx_);
    }

    void WarningConfigScreen::open(uint16_t                         signal_id,
                                   const WarningThresholdViewModel &vm,
                                   lv_obj_t *return_screen)
    {
        signal_id_     = signal_id;
        return_screen_ = return_screen;

        hi_value_  = vm.hi;
        lo_value_  = vm.lo;
        min_value_ = vm.min_value;
        max_value_ = vm.max_value;
        step_      = vm.step;
        decimals_  = vm.decimals;
        is_metric_ = vm.is_metric;
        snprintf(unit_label_, sizeof(unit_label_), "%s", vm.unit_label);
        refresh_value_labels();

        lv_label_set_text(name_lbl_, vm.name);

        if (vm.use_hi)
        {
            lv_obj_add_state(hi_sel_, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_remove_state(hi_sel_, LV_STATE_CHECKED);
        }

        if (vm.use_lo)
        {
            lv_obj_add_state(lo_sel_, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_remove_state(lo_sel_, LV_STATE_CHECKED);
        }

        set_enabled(hi_minus_btn_, vm.use_hi);
        set_enabled(hi_plus_btn_, vm.use_hi);
        set_enabled(lo_minus_btn_, vm.use_lo);
        set_enabled(lo_plus_btn_, vm.use_lo);

        lv_screen_load(screen_);
    }

    void WarningConfigScreen::set_enabled(lv_obj_t *obj, bool enabled)
    {
        if (enabled)
        {
            lv_obj_remove_state(obj, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_add_state(obj, LV_STATE_DISABLED);
        }
    }

    void WarningConfigScreen::step_value(bool is_hi, float direction)
    {
        float &value = is_hi ? hi_value_ : lo_value_;
        value += direction * step_;

        if (value < min_value_)
        {
            value = min_value_;
        }
        else if (value > max_value_)
        {
            value = max_value_;
        }

        refresh_value_labels();
    }

    void WarningConfigScreen::refresh_value_labels()
    {
        char buf[32];

        snprintf(
            buf, sizeof(buf), "%.*f %s", decimals_, hi_value_, unit_label_);
        lv_label_set_text(hi_value_lbl_, buf);

        snprintf(
            buf, sizeof(buf), "%.*f %s", decimals_, lo_value_, unit_label_);
        lv_label_set_text(lo_value_lbl_, buf);
    }

    void WarningConfigScreen::hi_toggle_cb(lv_event_t *e)
    {
        auto *self
            = static_cast<WarningConfigScreen *>(lv_event_get_user_data(e));
        bool on = lv_obj_has_state(self->hi_sel_, LV_STATE_CHECKED);
        set_enabled(self->hi_minus_btn_, on);
        set_enabled(self->hi_plus_btn_, on);
    }

    void WarningConfigScreen::lo_toggle_cb(lv_event_t *e)
    {
        auto *self
            = static_cast<WarningConfigScreen *>(lv_event_get_user_data(e));
        bool on = lv_obj_has_state(self->lo_sel_, LV_STATE_CHECKED);
        set_enabled(self->lo_minus_btn_, on);
        set_enabled(self->lo_plus_btn_, on);
    }

    void WarningConfigScreen::step_button_cb(lv_event_t *e)
    {
        auto *ctx = static_cast<StepButtonCtx *>(lv_event_get_user_data(e));
        lv_event_code_t code = lv_event_get_code(e);

        if (code == LV_EVENT_PRESSED)
        {
            ctx->stepped_by_hold = false;
        }
        else if (code == LV_EVENT_LONG_PRESSED
                 || code == LV_EVENT_LONG_PRESSED_REPEAT)
        {
            ctx->stepped_by_hold = true;
            ctx->screen->step_value(ctx->is_hi, ctx->direction);
        }
        else if (code == LV_EVENT_CLICKED)
        {
            /**
             * Only count release as a "step" press if the count didn't step
             * recently.
             */
            if (!ctx->stepped_by_hold)
            {
                ctx->screen->step_value(ctx->is_hi, ctx->direction);
            }
            ctx->stepped_by_hold = false;
        }
    }

    void WarningConfigScreen::done_cb_handler(lv_event_t *e)
    {
        auto *self
            = static_cast<WarningConfigScreen *>(lv_event_get_user_data(e));

        WarningThresholdViewModel result {};
        result.use_hi    = lv_obj_has_state(self->hi_sel_, LV_STATE_CHECKED);
        result.use_lo    = lv_obj_has_state(self->lo_sel_, LV_STATE_CHECKED);
        result.hi        = self->hi_value_;
        result.lo        = self->lo_value_;
        result.min_value = self->min_value_;
        result.max_value = self->max_value_;
        result.step      = self->step_;
        result.decimals  = self->decimals_;
        result.is_metric = self->is_metric_;

        if (self->done_cb_ != nullptr)
        {
            self->done_cb_(self->done_ctx_, self->signal_id_, result);
        }

        if (self->return_screen_ != nullptr)
        {
            lv_screen_load(self->return_screen_);
        }
    }

} // namespace gui
