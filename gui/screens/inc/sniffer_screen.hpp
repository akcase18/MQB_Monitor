/**
 * @file sniffer_screen.hpp
 * @brief What the panel shows while the dash is standing in as a USB CAN
 * sniffer for tools/monitor: the channel state, frame rate, total and
 * dropped frames, and one full-width Stop button. The gauges are paused
 * for the duration (see main.cpp's sniffer mode), so this is the only
 * thing on screen.
 *
 * Unlike every other screen this one is not built at boot. Sniffing is a
 * rare bench/debug session, so the screen is created on entry and deleted
 * on exit (create()/destroy()) and costs no LVGL memory the rest of the
 * time. There is no X close button: Stop is the one way out from the
 * panel, and it's sized like LoggingScreen's toggle so it can't be missed.
 *
 * Driven by a POD view-model with zero core::/platform:: dependency, like
 * the rest of gui/screens.
 */
#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "theme.hpp"

namespace gui
{

    enum class SnifferChannel : uint8_t
    {
        /**
         * Confirmed on the panel, but the host hasn't sent SLCAN's
         * open (L/O) yet -- or has closed it again.
         */
        Closed,
        ListenOnly,
        Normal
    };

    struct SnifferViewModel
    {
            SnifferChannel channel = SnifferChannel::Closed;

            uint32_t frames_per_sec = 0;
            uint32_t total_frames   = 0;

            /**
             * Frames lost because the host fell behind. Any nonzero
             * value means the capture has gaps, so it's painted in
             * warn_hi.
             */
            uint32_t dropped_frames = 0;
    };

    class SnifferScreen
    {
        public:
            /**
             * Fired when Stop is pressed. Runs inside the button's own
             * click handler, so the caller must not destroy() from it
             * directly -- defer with lv_async_call().
             */
            using StopCallback = void (*)(void *ctx);

            /**
             * Builds the screen. Does not load it.
             */
            void create(const Theme &theme, StopCallback stop_cb, void *ctx);

            /**
             * Deletes the screen. It must not be the loaded screen.
             */
            void destroy();

            void refresh(const SnifferViewModel &vm);

            /**
             * Null when not created.
             */
            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            static void stop_clicked_cb(lv_event_t *e);

            const Theme *theme_ = nullptr;

            lv_obj_t *screen_ = nullptr;

            lv_obj_t *channel_value_ = nullptr;
            lv_obj_t *rate_value_    = nullptr;
            lv_obj_t *total_value_   = nullptr;
            lv_obj_t *dropped_value_ = nullptr;

            StopCallback stop_cb_  = nullptr;
            void        *stop_ctx_ = nullptr;
    };

} // namespace gui
