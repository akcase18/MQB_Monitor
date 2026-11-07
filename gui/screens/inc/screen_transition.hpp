/**
 * @file screen_transition.hpp
 * @brief Slide a screen in over the current one, the way
 * lv_screen_load_anim(LV_SCR_LOAD_ANIM_OVER_*) does, but cheaply enough
 * for a CPU-bound software renderer.
 */
#pragma once

#include <lvgl.h>

#include <stdint.h>

namespace gui
{

    /**
     * Drop-in for lv_screen_load_anim(screen, anim, time_ms, 0, false)
     * for the OVER_* animations. Any other `anim` goes straight to
     * lv_screen_load_anim().
     */
    void slide_screen_in(lv_obj_t             *screen,
                         lv_screen_load_anim_t anim,
                         uint32_t              time_ms);

} // namespace gui
