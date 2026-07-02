/**
 * @file settings_screen.hpp
 * @brief Day brightness slider. Ported from the old Settings_Screen, but
 * this screen never touches IDisplay itself (gui/screens has no
 * platform:: dependency) — it just reports the chosen percentage via
 * DoneCallback, and the composition root (src/main.cpp) is what actually
 * calls platform.display.set_backlight().
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{

    class SettingsScreen
    {
        public:
            using DoneCallback
                = void (*)(void *ctx, uint8_t day_brightness_pct);

            void init(const Theme &theme,
                      uint8_t      initial_day_brightness_pct,
                      DoneCallback done_cb,
                      void        *ctx);

            void open();

            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            static void done_cb_handler(lv_event_t *e);

            lv_obj_t *screen_            = nullptr;
            lv_obj_t *brightness_slider_ = nullptr;

            uint8_t day_brightness_pct_ = 100;

            DoneCallback done_cb_  = nullptr;
            void        *done_ctx_ = nullptr;
    };

} // namespace gui
