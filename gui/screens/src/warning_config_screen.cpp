#include "warning_config_screen.hpp"

#include <stdio.h>

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        constexpr lv_coord_t STEP_BTN_WIDTH  = 64;
        constexpr lv_coord_t STEP_BTN_HEIGHT = 56;
        /* Fixed, not LV_SIZE_CONTENT — the value label's text length varies
         * per signal/unit ("32 °F" vs "7000 rpm"), and a content-sized
         * label would resize the row around it, shifting the +/- buttons
         * left/right. A fixed width keeps both buttons at the same on-
         * screen position for every row and every signal; wide enough for
         * the longest realistic case ("369 lb-ft"). */
        constexpr lv_coord_t VALUE_LBL_WIDTH = 170;

        /* Layout is a top nav bar (name + close) freeing the rest of the
         * screen for two identical [label+switch] / [-/value/+] sections
         * — the old bottom-right checkmark button could collide with the
         * low-warning row's plus button once the row's true fixed width
         * was known (see VALUE_LBL_WIDTH's comment); moving exit into the
         * header removes that bottom-right hazard entirely.
         *
         * SECTION_DIVIDER_Y sits exactly halfway between HEADER_DIVIDER_Y
         * and the screen's bottom edge, so the High and Low sections are
         * equal-height boxes ([header, divider] and [divider, bottom]);
         * everything below it mirrors High's internal rhythm (divider ->
         * label 14, label -> row 38, row height 56) so the trailing gap
         * after each section's control row comes out equal too. */
        constexpr lv_coord_t HEADER_DIVIDER_Y  = 50;
        constexpr lv_coord_t HI_LABEL_Y        = 64;
        constexpr lv_coord_t HI_SWITCH_Y       = 66;
        constexpr lv_coord_t HI_ROW_Y          = 102;
        constexpr lv_coord_t SECTION_DIVIDER_Y = 185;
        constexpr lv_coord_t LO_LABEL_Y        = 199;
        constexpr lv_coord_t LO_SWITCH_Y       = 201;
        constexpr lv_coord_t LO_ROW_Y          = 237;

        void style_step_button(lv_obj_t *btn, const Theme &theme)
        {
            lv_obj_set_size(btn, STEP_BTN_WIDTH, STEP_BTN_HEIGHT);
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

        lv_obj_t *create_control_row(lv_obj_t *parent, lv_coord_t y_offset)
        {
            lv_obj_t *row = lv_obj_create(parent);
            lv_obj_remove_style_all(row);
            lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row,
                                  LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row, 20, 0);
            lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y_offset);
            return row;
        }
    } // namespace

    void WarningConfigScreen::init(const Theme &theme,
                                   DoneCallback done_cb,
                                   void        *ctx)
    {
        done_cb_  = done_cb;
        done_ctx_ = ctx;

        screen_ = create_screen(theme);

        /* Nav bar: signal name + close, standing in for the bottom-right
         * checkmark every other screen uses (see the file doc comment). */
        name_lbl_ = lv_label_create(screen_);
        lv_obj_align(name_lbl_, LV_ALIGN_TOP_LEFT, 20, 12);
        lv_obj_set_style_text_color(name_lbl_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(name_lbl_, theme.typography.title, 0);

        DoneButton close = create_close_button(screen_, theme);
        close_btn_       = close.btn;
        lv_obj_add_event_cb(
            close_btn_, done_cb_handler, LV_EVENT_CLICKED, this);

        create_divider(screen_, theme, HEADER_DIVIDER_Y);

        hi_label_ = lv_label_create(screen_);
        lv_label_set_text(hi_label_, "High Warning");
        lv_obj_align(hi_label_, LV_ALIGN_TOP_LEFT, 20, HI_LABEL_Y);
        lv_obj_set_style_text_color(hi_label_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(hi_label_, theme.typography.ui, 0);

        hi_sel_ = lv_switch_create(screen_);
        lv_obj_align(hi_sel_, LV_ALIGN_TOP_RIGHT, -20, HI_SWITCH_Y);
        lv_obj_set_style_bg_color(hi_sel_,
                                  theme.palette.accent,
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(
            hi_sel_, hi_toggle_cb, LV_EVENT_VALUE_CHANGED, this);

        lv_obj_t *hi_row = create_control_row(screen_, HI_ROW_Y);
        hi_minus_btn_    = lv_btn_create(hi_row);
        style_step_button(hi_minus_btn_, theme);
        lv_obj_t *hi_minus_lbl = lv_label_create(hi_minus_btn_);
        lv_obj_set_style_text_font(hi_minus_lbl, theme.typography.symbol_lg, 0);
        lv_label_set_text(hi_minus_lbl, LV_SYMBOL_MINUS);
        lv_obj_center(hi_minus_lbl);

        hi_value_lbl_ = lv_label_create(hi_row);
        lv_obj_set_style_text_color(hi_value_lbl_, theme.palette.fg, 0);
        /* Not data_sm — that's a digits-only font (see theme.hpp) and this
         * label's text includes a unit suffix (e.g. "80 °F"). */
        lv_obj_set_style_text_font(hi_value_lbl_, theme.typography.ui, 0);
        lv_obj_set_style_text_align(hi_value_lbl_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(hi_value_lbl_, VALUE_LBL_WIDTH);

        hi_plus_btn_ = lv_btn_create(hi_row);
        style_step_button(hi_plus_btn_, theme);
        lv_obj_t *hi_plus_lbl = lv_label_create(hi_plus_btn_);
        lv_obj_set_style_text_font(hi_plus_lbl, theme.typography.symbol_lg, 0);
        lv_label_set_text(hi_plus_lbl, LV_SYMBOL_PLUS);
        lv_obj_center(hi_plus_lbl);

        create_divider(screen_, theme, SECTION_DIVIDER_Y);

        lo_label_ = lv_label_create(screen_);
        lv_label_set_text(lo_label_, "Low Warning");
        lv_obj_align(lo_label_, LV_ALIGN_TOP_LEFT, 20, LO_LABEL_Y);
        lv_obj_set_style_text_color(lo_label_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(lo_label_, theme.typography.ui, 0);

        lo_sel_ = lv_switch_create(screen_);
        lv_obj_align(lo_sel_, LV_ALIGN_TOP_RIGHT, -20, LO_SWITCH_Y);
        lv_obj_set_style_bg_color(lo_sel_,
                                  theme.palette.accent,
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(
            lo_sel_, lo_toggle_cb, LV_EVENT_VALUE_CHANGED, this);

        lv_obj_t *lo_row = create_control_row(screen_, LO_ROW_Y);
        lo_minus_btn_    = lv_btn_create(lo_row);
        style_step_button(lo_minus_btn_, theme);
        lv_obj_t *lo_minus_lbl = lv_label_create(lo_minus_btn_);
        lv_obj_set_style_text_font(lo_minus_lbl, theme.typography.symbol_lg, 0);
        lv_label_set_text(lo_minus_lbl, LV_SYMBOL_MINUS);
        lv_obj_center(lo_minus_lbl);

        lo_value_lbl_ = lv_label_create(lo_row);
        lv_obj_set_style_text_color(lo_value_lbl_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(lo_value_lbl_, theme.typography.ui, 0);
        lv_obj_set_style_text_align(lo_value_lbl_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(lo_value_lbl_, VALUE_LBL_WIDTH);

        lo_plus_btn_ = lv_btn_create(lo_row);
        style_step_button(lo_plus_btn_, theme);
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
            /* CLICKED still fires on release even after a long press —
             * only take it as a step if the hold path didn't already step
             * this press cycle (see the struct's doc comment). */
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
