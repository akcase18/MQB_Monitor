/**
 * @file backlight.hpp
 * @brief Keeps the panel dark while something ugly is happening on it.
 *
 * A small state machine with its own LVGL timer, lifted out of the
 * composition root because it is a subsystem rather than wiring: it owns
 * policy (when is a transition "over"?), it owns state across many call
 * sites, and none of that is a decision about how this particular product
 * is assembled. The root now just tells it what happened -- a flush
 * landed, a screen rebuild is starting, the user moved the brightness
 * slider -- and never sees the states.
 *
 * Product-neutral: it talks to IDisplay's backlight pair and to LVGL, and
 * knows nothing about which screens exist. Both composition roots use it
 * unchanged.
 *
 * The full reasoning for *why* the two blanking paths wait on different
 * signals -- and why the obvious unification does not work -- is in
 * backlight.cpp, next to the code it constrains.
 *
 * **None of this is observable in the simulator.** `SimulatorDisplay`'s
 * set_backlight()/set_backlight_off() are deliberate no-ops -- there is no
 * backlight on a dev host, and blanking the window would hide the very
 * rebuild being worked on. So the window renders identically whether this
 * state machine is correct, broken, or deleted outright. A change here is
 * verifiable only on hardware; a clean simulator run says nothing about it.
 * Backlight is hardware-only in the same way real CAN traffic is.
 */
#pragma once

#include <stdint.h>

namespace app
{
    namespace backlight
    {

        /**
         * Arms the watchdog that guarantees the panel is eventually lit.
         *
         * Call once from setup(), after lv_init() -- it creates an
         * lv_timer. The panel is already dark at that point (a display
         * driver's begin() holds the pin low through power-on garbage);
         * this exists purely so a boot where the expected render passes
         * never arrive still ends with a lit dashboard rather than a
         * black one.
         */
        void arm();

        /**
         * Sets the brightness the panel shows when lit -- immediately if
         * it is lit, and on the way back up if it is not, so a slider
         * move that lands mid-blank is neither lost nor allowed to light
         * the panel early.
         */
        void set_brightness(uint8_t pct);

        /**
         * Kills the light until whatever is about to happen on screen has
         * finished. Something must walk the state forward afterwards --
         * see begin_settle_if_blanked(), the counterpart -- or the panel
         * stays dark until the backstop fires.
         */
        void blank_for_transition();

        /**
         * Hands a blanked panel over to the quiet-period test: the ugly
         * part is done, now wait for rendering to stop.
         *
         * Guarded so that repeating it cannot restart the wait for a
         * panel that is already lit -- the screen-loaded event this hangs
         * off fires on every trip into that screen, not only the one
         * following a blank.
         *
         * @returns True if a blank was actually handed over.
         */
        bool begin_settle_if_blanked();

        /**
         * Reports a completed flush from the display flush callback.
         *
         * @param completed_pass True on the flush that finishes a full
         *                       render pass (lv_display_flush_is_last).
         *                       Boot counts these; see backlight.cpp for
         *                       why it counts passes rather than timing
         *                       them.
         */
        void note_flush(bool completed_pass);

    } // namespace backlight
} // namespace app
