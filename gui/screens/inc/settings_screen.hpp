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
 * for Done). Orientation has no live preview target the way brightness
 * does (InfoScreen, where it actually matters, isn't on screen while
 * Settings is open) — it's captured locally and only delivered through
 * OrientationConfirmCallback.
 *
 * Layout follows WarningConfigScreen's pattern: a top nav bar (title + X
 * close, instead of the bottom-right checkmark other screens use) over two
 * equal-height sections (Brightness, Panels) separated by full-width
 * dividers, plus a compact tappable-row footer (matching ConfiguratorScreen's
 * Signal/Warning Thresholds rows) — Theme (half-width, hands off to
 * ThemePickerScreen) and Orientation (the other half, a tap toggle, not a
 * navigation row) share one row, then WiFi (full width, hands off to
 * WirelessSetupScreen) — rather than embedding any of their controls
 * here. Logging is deliberately NOT here: it lives on its own
 * LoggingScreen, reached by swiping up from InfoScreen, since burying a
 * toggle three screens deep defeats the point of it being quick to
 * flip. The Brightness section's one slider edits whichever of
 * day/night the toggle currently selects, rather than showing two sliders
 * — day and night are rarely tuned side by side. A tap on the Orientation
 * toggle doesn't flip orientation_ directly — it opens an lv_msgbox asking
 * the user to confirm, since physically rotating the panel is a boot-
 * time-only change (see platform.hpp's DisplayOrientation) that won't
 * visibly do anything until a restart, and a silent flip with no visible
 * effect would look broken. That popup is what guards against an
 * accidental tap actually changing anything, so the button itself no
 * longer needs a long-press to do the same job. Confirm fires
 * OrientationConfirmCallback immediately — persisted right then, the same
 * way ThemePickerScreen's own pick is persisted the moment it's chosen
 * rather than waiting for this screen's own Done — since the popup itself
 * already reads as the commit step; saving it again only on Done (and
 * losing it on a power-cycle before Done was ever pressed) would
 * contradict what the popup just told the user.
 */
#pragma once

#include <lvgl.h>

#include "layout.hpp"
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
            /** Fired when Confirm is pressed on the orientation popup —
             * the caller should persist this immediately, not wait for
             * Done (see the file doc comment above). */
            using OrientationConfirmCallback
                = void (*)(void *ctx, LayoutOrientation orientation);

            void init(const Theme              &theme,
                      uint8_t                   initial_day_brightness_pct,
                      uint8_t                   initial_night_brightness_pct,
                      uint8_t                   initial_num_panels,
                      uint32_t                  initial_accent_color,
                      LayoutOrientation         initial_orientation,
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
                      OrientationConfirmCallback orientation_confirm_cb,
                      void                      *orientation_confirm_ctx);

            void open();

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
            static void orientation_clicked_cb(lv_event_t *e);
            static void orientation_confirm_cb(lv_event_t *e);
            static void orientation_cancel_cb(lv_event_t *e);

            /** Pulls the live slider and panel-count matrix into
             * day_/night_brightness_pct_/num_panels_, then hands all three
             * to commit_cb_. Runs on every exit from this screen, not just
             * Done — see CommitCallback. */
            void commit_pending();

            void refresh_theme_swatch();
            /** Sets the toggle button's glyph to match showing_night_. */
            void refresh_day_night_icon();
            /** Sets the toggle button's "H"/"V" letter to match
             * orientation_. */
            void refresh_orientation_label();
            /** Stores the slider's current value into whichever of day_
             * brightness_pct_/night_brightness_pct_ the toggle currently
             * selects — called before every read of those two members, so
             * an edit made just before Done/toggling/closing is never
             * lost. */
            void capture_slider_value();

            /* Stashed only so orientation_clicked_cb() (fired well
             * after init()) can style the confirm popup consistently
             * with the rest of the app rather than leaving it in LVGL's
             * default light-mode look. */
            const Theme *theme_           = nullptr;

            lv_obj_t *screen_             = nullptr;
            lv_obj_t *brightness_slider_  = nullptr;
            lv_obj_t *day_night_btn_      = nullptr;
            lv_obj_t *day_night_icon_lbl_ = nullptr;
            lv_obj_t *panel_count_matrix_ = nullptr;
            lv_obj_t *theme_swatch_       = nullptr;
            lv_obj_t *orientation_btn_            = nullptr;
            lv_obj_t *orientation_lbl_            = nullptr;
            /* Transient -- created in orientation_clicked_cb(),
             * destroyed by whichever of orientation_confirm_cb()/
             * orientation_cancel_cb() fires next. Only valid while the
             * popup is on screen. */
            lv_obj_t *orientation_confirm_mbox_   = nullptr;
            LayoutOrientation pending_orientation_
                = LayoutOrientation::Horizontal;

            uint8_t           day_brightness_pct_   = 100;
            uint8_t           night_brightness_pct_ = 20;
            bool              showing_night_        = false;
            uint8_t           num_panels_           = 5;
            uint32_t          accent_color_         = 0;
            LayoutOrientation orientation_ = LayoutOrientation::Horizontal;

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
            OrientationConfirmCallback orientation_confirm_cb_  = nullptr;
            void                      *orientation_confirm_ctx_ = nullptr;
    };

} // namespace gui
