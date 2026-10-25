/**
 * @file settings_screen.hpp
 * @brief Day/night brightness slider (toggle-selected, see below) plus
 * panel-count picker. Ported from the old Settings_Screen, but this screen
 * never touches IDisplay itself (gui/screens has no platform:: dependency)
 * — it reports chosen values via CommitCallback (fired on every exit from
 * the screen, including the Theme and WiFi hand-offs, not just the X
 * button) and PreviewBrightnessCallback (fired immediately on every slider
 * move or Day/Night toggle, so the composition root can push it straight
 * to platform.display.set_backlight() for live feedback without waiting
 * for Done). Orientation isn't edited here at all — its row hands off to
 * OrientationScreen, which reports its own changes.
 *
 * Layout follows WarningConfigScreen's pattern: a top nav bar (title + X
 * close, instead of the bottom-right checkmark other screens use) over two
 * equal-height sections (Brightness, Panels) separated by full-width
 * dividers, plus a compact tappable-row footer (matching ConfiguratorScreen's
 * Signal/Warning Thresholds rows) — Theme (half-width, hands off to
 * ThemePickerScreen) and Orientation (the other half, hands off to
 * OrientationScreen) share one row, then WiFi (full width, hands off to
 * WirelessSetupScreen) — rather than embedding any of their controls
 * here. Logging is deliberately NOT here: it lives on its own
 * LoggingScreen, reached by swiping up from InfoScreen, since burying a
 * toggle three screens deep defeats the point of it being quick to
 * flip. The Brightness section's one slider edits whichever of
 * day/night the toggle currently selects, rather than showing two sliders
 * — day and night are rarely tuned side by side.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{

    class SettingsScreen
    {
        public:
            /** Fired every time this screen hands control away — the X
             * button, but also the Theme and WiFi rows. Those two used to
             * navigate off without reporting anything, so a panel-count or
             * brightness edit made just before tapping one was silently
             * dropped: the Theme row's pick rebuilds every screen from the
             * config store, and the WiFi row's AP hands the phone whatever
             * the store currently holds. Both then read back values the
             * user had already changed on screen. */
            using CommitCallback = void (*)(void   *ctx,
                                            uint8_t day_brightness_pct,
                                            uint8_t night_brightness_pct,
                                            uint8_t num_panels);
            /** Fired only when the X button closes the screen, after
             * CommitCallback — purely the caller's cue to navigate back to
             * the dashboard, which a hand-off to Theme/WiFi must not do. */
            using DoneCallback = void (*)(void *ctx);
            /** Fired immediately on every slider move and Day/Night
             * toggle — lets the caller preview the change on the real
             * backlight without waiting for Done. */
            using PreviewBrightnessCallback
                = void (*)(void *ctx, uint8_t brightness_pct);
            /** Fired when the Theme row is tapped. */
            using PickThemeCallback = void (*)(void *ctx);
            /** Fired when the WiFi row is tapped. */
            using PickWirelessCallback = void (*)(void *ctx);
            /** Fired when the Orientation row is tapped. */
            using PickOrientationCallback = void (*)(void *ctx);

            void init(const Theme              &theme,
                      uint8_t                   initial_day_brightness_pct,
                      uint8_t                   initial_night_brightness_pct,
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
             * with, for when the config changed behind its back (a phone-
             * side edit). Without it, the next exit's commit would write
             * the stale values straight back over the new ones.
             */
            void set_values(uint8_t           day_brightness_pct,
                            uint8_t           night_brightness_pct,
                            uint8_t           num_panels,
                            uint32_t          accent_color);

            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            static void done_cb_handler(lv_event_t *e);
            static void brightness_changed_cb(lv_event_t *e);
            static void day_night_clicked_cb(lv_event_t *e);
            static void pick_theme_cb_handler(lv_event_t *e);
            static void pick_wireless_cb_handler(lv_event_t *e);
            static void pick_orientation_cb_handler(lv_event_t *e);

            /** Pulls the live slider and panel-count matrix into
             * day_/night_brightness_pct_/num_panels_, then hands all three
             * to commit_cb_. Runs on every exit from this screen, not just
             * Done — see CommitCallback. */
            void commit_pending();

            /** Pushes the cached values below into every widget. */
            void sync_widgets();
            void refresh_theme_swatch();
            /** Sets the toggle button's glyph to match showing_night_. */
            void refresh_day_night_icon();
            /** Stores the slider's current value into whichever of day_
             * brightness_pct_/night_brightness_pct_ the toggle currently
             * selects — called before every read of those two members, so
             * an edit made just before Done/toggling/closing is never
             * lost. */
            void capture_slider_value();

            lv_obj_t *screen_             = nullptr;
            lv_obj_t *brightness_slider_  = nullptr;
            lv_obj_t *day_night_btn_      = nullptr;
            lv_obj_t *day_night_icon_lbl_ = nullptr;
            lv_obj_t *panel_count_matrix_ = nullptr;
            lv_obj_t *theme_swatch_       = nullptr;

            uint8_t           day_brightness_pct_   = 100;
            uint8_t           night_brightness_pct_ = 20;
            bool              showing_night_        = false;
            uint8_t           num_panels_           = 5;
            uint32_t          accent_color_         = 0;

            CommitCallback            commit_cb_         = nullptr;
            void                     *commit_ctx_        = nullptr;
            DoneCallback              done_cb_           = nullptr;
            void                     *done_ctx_          = nullptr;
            PreviewBrightnessCallback preview_cb_        = nullptr;
            void                     *preview_ctx_       = nullptr;
            PickThemeCallback         pick_theme_cb_     = nullptr;
            void                     *pick_theme_ctx_    = nullptr;
            PickWirelessCallback      pick_wireless_cb_  = nullptr;
            void                     *pick_wireless_ctx_ = nullptr;
            PickOrientationCallback   pick_orientation_cb_  = nullptr;
            void                     *pick_orientation_ctx_ = nullptr;
    };

} // namespace gui
