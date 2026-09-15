/**
 * @file lvgl_runtime.hpp
 * @brief LVGL bring-up: draw buffers, the display, the tick source, and
 * (optionally) a pointer input device.
 *
 * All of this is the same on every product -- it is the boilerplate of
 * putting LVGL on top of platform.hpp's IDisplay/ITouch/IClock, not a
 * decision about how a particular dashboard is assembled. The composition
 * root asks for it in one call and gets on with wiring screens.
 *
 * The one genuine product difference is `with_pointer_indev`. A board with
 * no touchscreen must not create one: an lv_indev with nothing behind it
 * would poll a no-op ITouch forever, and on the simulator that used to be
 * the only thing draining the SDL event queue (see simulator_events.hpp
 * for how that was fixed).
 */
#pragma once

#include <stdint.h>

namespace app
{
    namespace lvgl_runtime
    {

        struct Options
        {
                /** Panel resolution, post-rotation -- ask IDisplay rather
                 * than assuming, since orientation is applied first. */
                uint16_t width;
                uint16_t height;

                /**
                 * Create an LV_INDEV_TYPE_POINTER fed by ITouch. False on
                 * a touchless product, whose interaction comes from
                 * core/input driving app logic directly rather than
                 * through LVGL.
                 */
                bool with_pointer_indev;
        };

        /**
         * Calls lv_init(), installs the tick callback, creates the display
         * with its partial-render buffers, and optionally the pointer
         * device. Call once, after the platform's display/touch begin().
         */
        void begin(const Options &options);

    } // namespace lvgl_runtime
} // namespace app
