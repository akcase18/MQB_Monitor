/**
 * @file toast_overlay.hpp
 * @brief A transient message over whatever is on screen, on
 * lv_layer_top().
 *
 * The touchless product has one screen and no navigation, so everything a
 * button press needs to acknowledge -- brightness changed, units changed,
 * peaks reset, warning acknowledged, what the gestures are -- has nowhere
 * to go. A screen apiece would be far more machinery than any of those
 * deserve, and navigation is exactly what a two-button dashboard must not
 * grow.
 *
 * So they share one overlay. It lives on `lv_layer_top()` rather than on a
 * screen, which means it survives a screen load or a full rebuild without
 * being recreated, and it sits above the panel grid without being part of
 * its layout.
 *
 * Deliberately not clickable: it never intercepts input, on any product.
 * Nothing here is a control, and a message that can be tapped is a message
 * that can be tapped by accident.
 *
 * Product-neutral and core::-free, like every other gui/widgets type: the
 * composition root formats the text and decides how long it stays.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{

    /** Long enough to read a short line at a glance, short enough not to
     * sit over a gauge. The build plan's ~2s. */
    constexpr uint32_t TOAST_DEFAULT_MS = 2000;

    class ToastOverlay
    {
        public:
            /** Creates the (hidden) overlay on lv_layer_top(). Call once,
             * after LVGL is up. Safe to call again; it rebuilds. */
            void init(const Theme &theme);

            /**
             * Shows `text` -- which may contain newlines, for a multi-line
             * hint -- and restarts the dismiss timer.
             *
             * @param duration_ms 0 keeps it up until hide() is called,
             *                    which is what an armed confirmation
             *                    countdown wants.
             */
            void show(const char *text, uint32_t duration_ms = TOAST_DEFAULT_MS);

            void hide();

            bool visible() const;

        private:
            static void timeout_cb(lv_timer_t *timer);

            lv_obj_t   *box_   = nullptr;
            lv_obj_t   *label_ = nullptr;
            lv_timer_t *timer_ = nullptr;
    };

} // namespace gui
