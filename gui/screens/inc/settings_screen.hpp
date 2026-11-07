/**
 * @file settings_screen.hpp
 * @brief Brightness slider with a Day/Night/Auto mode toggle (see below)
 * plus panel-count picker.
 *
 * Layout follows WarningConfigScreen's pattern: a top nav bar (title + X
 * close) over two equal-height sections (Brightness, Panels) separated by
 * full-width dividers, plus a compact tappable-row footer: Theme (half-width,
 * hands off to ThemePickerScreen), Orientation (the other half, hands off to
 * OrientationScreen), then WiFi (full width, hands off to WirelessSetupScreen).
 *
 * The Brightness section's one slider edits whichever of day/night the toggle
 * currently selects, rather than showing two sliders. In Auto the backlight
 * follows the car's dimmer, and the slider shifts it: the midpoint shows the
 * dimmer's level as it is, either end up to 25% brighter or dimmer.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{

    enum class BrightnessMode
    {
        Day,
        Night,
        Auto
    };

    class SettingsScreen
    {
        public:
            using CommitCallback = void (*)(void          *ctx,
                                            uint8_t        day_brightness_pct,
                                            uint8_t        night_brightness_pct,
                                            uint8_t        auto_brightness_pct,
                                            BrightnessMode brightness_mode,
                                            uint8_t        num_panels);
            using DoneCallback   = void (*)(void *ctx);
            using PreviewBrightnessCallback = void (*)(void          *ctx,
                                                       BrightnessMode mode,
                                                       uint8_t brightness_pct);
            using PickThemeCallback         = void (*)(void *ctx);
            using PickWirelessCallback      = void (*)(void *ctx);
            using PickOrientationCallback   = void (*)(void *ctx);

            void init(const Theme              &theme,
                      const char               *firmware_version,
                      uint8_t                   initial_day_brightness_pct,
                      uint8_t                   initial_night_brightness_pct,
                      uint8_t                   initial_auto_brightness_pct,
                      BrightnessMode            initial_brightness_mode,
                      uint8_t                   initial_num_panels,
                      uint32_t                  initial_accent_color,
                      CommitCallback            commit_cb,
                      void                     *commit_ctx,
                      DoneCallback              done_cb,
                      void                     *done_ctx,
                      PreviewBrightnessCallback preview_cb,
                      void                     *preview_ctx,
                      PickThemeCallback         pick_theme_cb,
                      void                     *pick_theme_ctx,
                      PickWirelessCallback      pick_wireless_cb,
                      void                     *pick_wireless_ctx,
                      PickOrientationCallback   pick_orientation_cb,
                      void                     *pick_orientation_ctx);

            void open();

            /**
             * Replaces the values this screen was built or last opened
             * with. Used for phone-configurator edits.
             */
            void set_values(uint8_t        day_brightness_pct,
                            uint8_t        night_brightness_pct,
                            uint8_t        auto_brightness_pct,
                            BrightnessMode brightness_mode,
                            uint8_t        num_panels,
                            uint32_t       accent_color);

            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            static void done_cb_handler(lv_event_t *e);
            static void brightness_changed_cb(lv_event_t *e);
            static void mode_clicked_cb(lv_event_t *e);
            static void pick_theme_cb_handler(lv_event_t *e);
            static void pick_wireless_cb_handler(lv_event_t *e);
            static void pick_orientation_cb_handler(lv_event_t *e);

            /**
             * Commits live values shown to actual values.
             */
            void commit_pending();

            void    sync_widgets();
            void    refresh_theme_swatch();
            void    refresh_mode_widgets();
            void    capture_slider_value();
            uint8_t slider_value_for_mode() const;

            lv_obj_t *screen_             = nullptr;
            lv_obj_t *brightness_slider_  = nullptr;
            lv_obj_t *mode_btn_           = nullptr;
            lv_obj_t *mode_lbl_           = nullptr;
            lv_obj_t *panel_count_matrix_ = nullptr;
            lv_obj_t *theme_swatch_       = nullptr;

            uint8_t        day_brightness_pct_   = 100;
            uint8_t        night_brightness_pct_ = 20;
            uint8_t        auto_brightness_pct_  = 50;
            BrightnessMode mode_                 = BrightnessMode::Day;

            /**
             * Sun/moon glyphs, and the text font Auto's label uses.
             */
            const lv_font_t *icon_font_      = nullptr;
            const lv_font_t *mode_text_font_ = nullptr;
            uint8_t          num_panels_     = 5;
            uint32_t         accent_color_   = 0;

            CommitCallback            commit_cb_            = nullptr;
            void                     *commit_ctx_           = nullptr;
            DoneCallback              done_cb_              = nullptr;
            void                     *done_ctx_             = nullptr;
            PreviewBrightnessCallback preview_cb_           = nullptr;
            void                     *preview_ctx_          = nullptr;
            PickThemeCallback         pick_theme_cb_        = nullptr;
            void                     *pick_theme_ctx_       = nullptr;
            PickWirelessCallback      pick_wireless_cb_     = nullptr;
            void                     *pick_wireless_ctx_    = nullptr;
            PickOrientationCallback   pick_orientation_cb_  = nullptr;
            void                     *pick_orientation_ctx_ = nullptr;
    };

} // namespace gui
