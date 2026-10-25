/**
 * @file orientation_screen.hpp
 * @brief Display orientation, opened from SettingsScreen's Orientation
 * row: a Horizontal/Vertical layout picker, plus a Rotate 180 button that
 * turns whichever of those is active upside down (for a panel mounted the
 * other way up).
 *
 * The two apply very differently, which is why they're separate controls
 * rather than a four-way picker. Rotate is live: it fires RotateCallback
 * on tap, and the composition root re-rotates the panel and touch on the
 * spot (a 180-degree turn keeps the resolution, so nothing needs
 * rebuilding). A layout change swaps width and height, which every screen
 * and the lv_display_t itself are sized from at boot -- so picking the
 * other layout opens a confirm popup first, and Confirm fires
 * LayoutConfirmCallback, whose caller persists it and restarts the board.
 * The popup is what keeps a stray tap from rebooting the dash; Rotate
 * needs no such guard, since tapping it again undoes it.
 *
 * Like every screen here, it never touches IDisplay itself (gui/screens
 * has no platform:: dependency).
 */
#pragma once

#include <lvgl.h>

#include "layout.hpp"
#include "theme.hpp"

namespace gui
{

    class OrientationScreen
    {
        public:
            /** Fired when Confirm is pressed on the layout popup. The
             * caller persists `orientation` and restarts -- this screen
             * doesn't expect control back. */
            using LayoutConfirmCallback
                = void (*)(void *ctx, LayoutOrientation orientation);
            /** Fired on every tap of Rotate 180. */
            using RotateCallback = void (*)(void *ctx);

            void init(const Theme          &theme,
                      LayoutConfirmCallback layout_confirm_cb,
                      void                 *layout_confirm_ctx,
                      RotateCallback        rotate_cb,
                      void                 *rotate_ctx);

            /** Opens the screen with `orientation` checked -- the layout
             * the panel is actually in right now. */
            void open(LayoutOrientation orientation, lv_obj_t *return_screen);

            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            static void close_cb_handler(lv_event_t *e);
            static void layout_changed_cb(lv_event_t *e);
            static void layout_confirm_cb(lv_event_t *e);
            static void layout_cancel_cb(lv_event_t *e);
            static void rotate_clicked_cb(lv_event_t *e);

            /** Checks whichever matrix button matches orientation_. */
            void sync_layout_matrix();

            /* Stashed so layout_changed_cb() can style the popup it
             * opens long after init(). */
            const Theme *theme_ = nullptr;

            lv_obj_t *screen_        = nullptr;
            lv_obj_t *return_screen_ = nullptr;
            lv_obj_t *layout_matrix_ = nullptr;
            /* Transient -- created in layout_changed_cb(), destroyed by
             * whichever of layout_confirm_cb()/layout_cancel_cb() fires
             * next. Only valid while the popup is on screen. */
            lv_obj_t        *confirm_mbox_ = nullptr;
            LayoutOrientation pending_orientation_
                = LayoutOrientation::Horizontal;

            LayoutOrientation orientation_ = LayoutOrientation::Horizontal;

            LayoutConfirmCallback layout_confirm_cb_  = nullptr;
            void                 *layout_confirm_ctx_ = nullptr;
            RotateCallback        rotate_cb_          = nullptr;
            void                 *rotate_ctx_         = nullptr;
    };

} // namespace gui
