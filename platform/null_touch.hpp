/**
 * @file null_touch.hpp
 * @brief ITouch for a board with no touchscreen.
 *
 * read() always reports no touch, so a caller that polls it does nothing
 * rather than reading a controller that is not on the bus.
 *
 * **A board wiring this must also pass `with_pointer_indev: false` to
 * app::lvgl_runtime::begin().** An lv_indev created over this would poll
 * it at the input rate forever and never produce an event -- harmless on
 * hardware, and actively wrong on the desktop simulator, where the touch
 * driver used to be the only thing draining the SDL event queue. The null
 * driver is the honest answer for the interface; not creating the indev is
 * the honest answer for LVGL.
 *
 * Header-only and board-agnostic, like the other nulls here.
 */
#pragma once

#include "platform.hpp"

class NullTouch : public ITouch
{
    public:
        static NullTouch &instance()
        {
            static NullTouch s_instance;
            return s_instance;
        }

        void begin() override
        {
        }

        void set_orientation(DisplayOrientation orientation) override
        {
            (void)orientation;
        }

        bool read(TouchPoint &out) override
        {
            (void)out;
            return false;
        }
};
