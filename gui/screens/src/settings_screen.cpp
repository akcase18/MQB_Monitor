#include "settings_screen.hpp"

#include "screen_helpers.hpp"

namespace gui
{

    void SettingsScreen::init(const Theme &theme,
                              uint8_t      initial_day_brightness_pct,
                              DoneCallback done_cb,
                              void        *ctx)
    {
        day_brightness_pct_ = initial_day_brightness_pct;
        done_cb_            = done_cb;
        done_ctx_           = ctx;

        screen_ = create_screen(theme);

        brightness_slider_ = lv_slider_create(screen_);
        lv_obj_center(brightness_slider_);
        lv_slider_set_range(brightness_slider_, 0, 100);

        DoneButton done = create_done_button(screen_, theme);
        lv_obj_add_event_cb(done.btn, done_cb_handler, LV_EVENT_CLICKED, this);
    }

    void SettingsScreen::open()
    {
        lv_slider_set_value(
            brightness_slider_, day_brightness_pct_, LV_ANIM_OFF);
        lv_screen_load_anim(
            screen_, LV_SCR_LOAD_ANIM_OVER_BOTTOM, 220, 0, false);
    }

    void SettingsScreen::done_cb_handler(lv_event_t *e)
    {
        auto *self = static_cast<SettingsScreen *>(lv_event_get_user_data(e));

        self->day_brightness_pct_ = static_cast<uint8_t>(
            lv_slider_get_value(self->brightness_slider_));

        if (self->done_cb_ != nullptr)
        {
            self->done_cb_(self->done_ctx_, self->day_brightness_pct_);
        }
    }

} // namespace gui
