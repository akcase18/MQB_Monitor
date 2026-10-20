/**
 * @file screen_transition.hpp
 * @brief Slide a screen in over the current one, the way
 * lv_screen_load_anim(LV_SCR_LOAD_ANIM_OVER_*) does, but cheaply enough
 * for a CPU-bound software renderer.
 *
 * lv_screen_load_anim() moves the real screen, so every animation frame
 * re-renders every widget on it. On the ESP32-S3 dash a full-screen
 * render is ~30-40ms, which leaves a 220ms slide with 7-8 frames.
 *
 * With GUI_SNAPSHOT_TRANSITIONS, slide_screen_in() instead:
 *  1. renders `screen` once into an RGB565 snapshot (lv_snapshot_take),
 *  2. loads a bare transition screen holding that snapshot as an image,
 *     with display invalidation off -- so nothing is redrawn and the
 *     panel keeps showing the outgoing screen out of its own GRAM,
 *  3. animates the image. Each frame redraws only the area the image
 *     covers, and the image covers it opaquely, so the frame is a
 *     straight pixel copy with no widgets underneath,
 *  4. loads the real `screen` at the end -- one ordinary full redraw,
 *     pixel-identical to the snapshot except for anything that changed
 *     during the slide.
 *
 * The snapshot is a full screen of RGB565 (300KB at 480x320) from
 * lv_malloc. If it can't be had, or the flag is off (the Teensy, whose
 * renderer keeps up without it), this is plain lv_screen_load_anim().
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
