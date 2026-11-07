/**
 * @file theme_picker_screen.hpp
 * @brief Accent-color picker. A grid of gui::ACCENT_PRESETS swatches,
 * opened from SettingsScreen's Theme row. Tapping a swatch fires
 * DoneCallback immediately.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{

    class ThemePickerScreen
    {
        public:
            using DoneCallback = void (*)(void *ctx, uint32_t accent_color);

            void init(const Theme &theme, DoneCallback done_cb, void *ctx);

            /**
             * Opens the screen, marking whichever preset matches
             * `current_accent_color` (if any) as selected.
             */
            void open(uint32_t current_accent_color, lv_obj_t *return_screen);

            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            static void swatch_clicked_cb(lv_event_t *e);
            static void close_cb_handler(lv_event_t *e);

            lv_obj_t    *screen_                        = nullptr;
            lv_obj_t    *return_screen_                 = nullptr;
            lv_obj_t    *swatches_[ACCENT_PRESET_COUNT] = {};
            const Theme *theme_                         = nullptr;

            DoneCallback done_cb_  = nullptr;
            void        *done_ctx_ = nullptr;
    };

} // namespace gui
