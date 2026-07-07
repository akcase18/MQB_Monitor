/**
 * @file settings_screen.hpp
 * @brief Day/night brightness slider (toggle-selected, see below) plus
 * panel-count picker. Ported from the old Settings_Screen, but this screen
 * never touches IDisplay itself (gui/screens has no platform:: dependency)
 * — it reports chosen values via DoneCallback (persisted, applied on
 * close) and PreviewBrightnessCallback (fired immediately on every slider
 * move or Day/Night toggle, so the composition root can push it straight
 * to platform.display.set_backlight() for live feedback without waiting
 * for Done). Orientation isn't exposed here yet — vertical layouts are out
 * of scope for now (see the layout plan).
 *
 * Layout follows WarningConfigScreen's pattern: a top nav bar (title + X
 * close, instead of the bottom-right checkmark other screens use) over two
 * equal-height sections (Brightness, Panels) separated by full-width
 * dividers, plus a third compact Theme row (matching ConfiguratorScreen's
 * tappable-row style) that hands off to ThemePickerScreen rather than
 * embedding a color grid here. The Brightness section's one slider edits
 * whichever of day/night the toggle currently selects, rather than
 * showing two sliders — day and night are rarely tuned side by side.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{

    class SettingsScreen
    {
        public:
            using DoneCallback = void (*)(void   *ctx,
                                          uint8_t day_brightness_pct,
                                          uint8_t night_brightness_pct,
                                          uint8_t num_panels);
            /** Fired immediately on every slider move and Day/Night
             * toggle — lets the caller preview the change on the real
             * backlight without waiting for Done. */
            using PreviewBrightnessCallback
                = void (*)(void *ctx, uint8_t brightness_pct);
            /** Fired when the Theme row is tapped. */
            using PickThemeCallback = void (*)(void *ctx);

            void init(const Theme              &theme,
                      uint8_t                   initial_day_brightness_pct,
                      uint8_t                   initial_night_brightness_pct,
                      uint8_t                   initial_num_panels,
                      uint32_t                  initial_accent_color,
                      DoneCallback              done_cb,
                      void                     *done_ctx,
                      PreviewBrightnessCallback preview_cb,
                      void                     *preview_ctx,
                      PickThemeCallback         pick_theme_cb,
                      void                     *pick_theme_ctx);

            void open();

            /** Updates the Theme row's swatch preview — called after
             * ThemePickerScreen returns, before this screen is shown
             * again. */
            void set_accent_color(uint32_t accent_color);

            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            static void done_cb_handler(lv_event_t *e);
            static void brightness_changed_cb(lv_event_t *e);
            static void day_night_clicked_cb(lv_event_t *e);
            static void pick_theme_cb_handler(lv_event_t *e);

            void refresh_brightness_label();
            void refresh_theme_swatch();
            /** Sets the toggle button's glyph to match showing_night_. */
            void refresh_day_night_icon();
            /** Stores the slider's current value into whichever of day_
             * brightness_pct_/night_brightness_pct_ the toggle currently
             * selects — called before every read of those two members, so
             * an edit made just before Done/toggling/closing is never
             * lost. */
            void capture_slider_value();

            lv_obj_t *screen_               = nullptr;
            lv_obj_t *brightness_slider_    = nullptr;
            lv_obj_t *brightness_value_lbl_ = nullptr;
            lv_obj_t *day_night_btn_        = nullptr;
            lv_obj_t *day_night_icon_lbl_   = nullptr;
            lv_obj_t *panel_count_matrix_   = nullptr;
            lv_obj_t *theme_swatch_         = nullptr;

            uint8_t  day_brightness_pct_   = 100;
            uint8_t  night_brightness_pct_ = 20;
            bool     showing_night_        = false;
            uint8_t  num_panels_           = 5;
            uint32_t accent_color_         = 0;

            DoneCallback              done_cb_        = nullptr;
            void                     *done_ctx_       = nullptr;
            PreviewBrightnessCallback preview_cb_     = nullptr;
            void                     *preview_ctx_    = nullptr;
            PickThemeCallback         pick_theme_cb_  = nullptr;
            void                     *pick_theme_ctx_ = nullptr;
    };

} // namespace gui
