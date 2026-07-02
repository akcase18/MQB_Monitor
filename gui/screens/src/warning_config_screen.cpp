#include "warning_config_screen.hpp"

#include "screen_helpers.hpp"

namespace gui
{

    void WarningConfigScreen::init(const Theme &theme,
                                   DoneCallback done_cb,
                                   void        *ctx)
    {
        done_cb_  = done_cb;
        done_ctx_ = ctx;

        screen_ = create_screen(theme);

        master_sel_ = lv_checkbox_create(screen_);
        lv_checkbox_set_text(master_sel_, "Warning Active");
        lv_obj_align(master_sel_, LV_ALIGN_TOP_LEFT, 20, 20);
        lv_obj_set_style_text_color(master_sel_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(master_sel_, theme.typography.ui, 0);
        lv_obj_add_event_cb(
            master_sel_, master_toggle_cb, LV_EVENT_CLICKED, this);

        hi_sel_ = lv_checkbox_create(screen_);
        lv_checkbox_set_text(hi_sel_, "High Warning:");
        lv_obj_align(hi_sel_, LV_ALIGN_TOP_LEFT, 20, 60);
        lv_obj_set_style_text_color(hi_sel_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(hi_sel_, theme.typography.ui, 0);
        lv_obj_add_event_cb(hi_sel_, hi_toggle_cb, LV_EVENT_CLICKED, this);

        hi_slider_ = lv_slider_create(screen_);
        lv_obj_align(hi_slider_, LV_ALIGN_TOP_MID, 0, 100);
        lv_slider_set_range(hi_slider_, 0, 100);

        lo_sel_ = lv_checkbox_create(screen_);
        lv_checkbox_set_text(lo_sel_, "Low Warning:");
        lv_obj_align(lo_sel_, LV_ALIGN_TOP_LEFT, 20, 140);
        lv_obj_set_style_text_color(lo_sel_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(lo_sel_, theme.typography.ui, 0);
        lv_obj_add_event_cb(lo_sel_, lo_toggle_cb, LV_EVENT_CLICKED, this);

        lo_slider_ = lv_slider_create(screen_);
        lv_obj_align(lo_slider_, LV_ALIGN_TOP_MID, 0, 180);
        lv_slider_set_range(lo_slider_, 0, 100);

        DoneButton done = create_done_button(screen_, theme);
        lv_obj_add_event_cb(done.btn, done_cb_handler, LV_EVENT_CLICKED, this);
    }

    void WarningConfigScreen::open(uint16_t                         signal_id,
                                   const WarningThresholdViewModel &vm,
                                   lv_obj_t *return_screen)
    {
        signal_id_     = signal_id;
        return_screen_ = return_screen;

        bool master = vm.use_lo || vm.use_hi;
        if (master)
        {
            lv_obj_add_state(master_sel_, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_remove_state(master_sel_, LV_STATE_CHECKED);
        }

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

        lv_slider_set_value(
            hi_slider_, static_cast<int32_t>(vm.hi), LV_ANIM_OFF);
        lv_slider_set_value(
            lo_slider_, static_cast<int32_t>(vm.lo), LV_ANIM_OFF);

        set_enabled(hi_sel_, master);
        set_enabled(lo_sel_, master);
        set_enabled(hi_slider_, master && vm.use_hi);
        set_enabled(lo_slider_, master && vm.use_lo);

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

    void WarningConfigScreen::master_toggle_cb(lv_event_t *e)
    {
        auto *self
            = static_cast<WarningConfigScreen *>(lv_event_get_user_data(e));
        bool master = lv_obj_has_state(self->master_sel_, LV_STATE_CHECKED);

        set_enabled(self->hi_sel_, master);
        set_enabled(self->lo_sel_, master);
        set_enabled(self->hi_slider_,
                    master
                        && lv_obj_has_state(self->hi_sel_, LV_STATE_CHECKED));
        set_enabled(self->lo_slider_,
                    master
                        && lv_obj_has_state(self->lo_sel_, LV_STATE_CHECKED));
    }

    void WarningConfigScreen::hi_toggle_cb(lv_event_t *e)
    {
        auto *self
            = static_cast<WarningConfigScreen *>(lv_event_get_user_data(e));
        set_enabled(self->hi_slider_,
                    lv_obj_has_state(self->hi_sel_, LV_STATE_CHECKED));
    }

    void WarningConfigScreen::lo_toggle_cb(lv_event_t *e)
    {
        auto *self
            = static_cast<WarningConfigScreen *>(lv_event_get_user_data(e));
        set_enabled(self->lo_slider_,
                    lv_obj_has_state(self->lo_sel_, LV_STATE_CHECKED));
    }

    void WarningConfigScreen::done_cb_handler(lv_event_t *e)
    {
        auto *self
            = static_cast<WarningConfigScreen *>(lv_event_get_user_data(e));

        bool master = lv_obj_has_state(self->master_sel_, LV_STATE_CHECKED);

        WarningThresholdViewModel result {};
        result.use_hi
            = master && lv_obj_has_state(self->hi_sel_, LV_STATE_CHECKED);
        result.use_lo
            = master && lv_obj_has_state(self->lo_sel_, LV_STATE_CHECKED);
        result.hi = static_cast<float>(lv_slider_get_value(self->hi_slider_));
        result.lo = static_cast<float>(lv_slider_get_value(self->lo_slider_));

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
