/**
 * @file simulator_events.hpp
 * @brief The simulator's one SDL event pump, and the input state it
 * produces.
 *
 * This used to live inside SimulatorTouch::read(), which worked only
 * because touch happened to be polled every main-loop iteration. Two things
 * broke that: buttons need the same events, and a touchless product's
 * Platform supplies a no-op ITouch that is never polled at all -- so the
 * window would stop responding entirely, including to the close box.
 *
 * Now both drivers call pump() and then read whichever state they care
 * about. Draining is idempotent from a caller's point of view: whoever
 * calls first empties the queue, and the state both of them read was
 * updated by that same call.
 */
#pragma once

#include <stdint.h>

namespace simulator_events
{

    /**
     * Drains the SDL event queue and updates the state below. Safe to call
     * many times per iteration. Handles window close (exits the process,
     * via exit() so the window-position atexit handler still runs).
     */
    void pump();

    /* --- Mouse, in window pixel coordinates --------------------------- */

    bool    mouse_pressed();
    int16_t mouse_x();
    int16_t mouse_y();

    /* --- Keyboard ----------------------------------------------------- */

    /**
     * Whether the key bound to button `idx` is currently held. Keys 'a' and
     * 'b' drive buttons 0 and 1, with '1' and '2' accepted as aliases.
     *
     * Keyboard matters beyond convenience: a mouse has one cursor, so
     * chords (both buttons at once, see the gauge product's action map)
     * are physically unreachable by clicking. Holding a key and clicking
     * the other button works too.
     */
    bool button_key_down(uint8_t idx);

    /**
     * True once per press of the fault key ('f'), then false until it is
     * pressed again -- an edge, not a level.
     *
     * SimulatorAnalogInput uses it to cycle which channel is faking an
     * open circuit. Exposed as a raw edge rather than a channel number so
     * this file stays ignorant of how many channels a product has.
     */
    bool consume_fault_key();

    /**
     * Whether the pressure-drop key ('p') is currently held -- a level,
     * not an edge, because how *long* it is held is the whole point.
     *
     * SimulatorAnalogInput collapses oil pressure to near zero while it is
     * down. That is a different fault from the 'f' key's open circuit: an
     * open circuit is caught by the plausibility band and produces no
     * reading at all, whereas this is a perfectly believable reading of a
     * genuinely bad pressure -- which is what a warning threshold is for,
     * and the only way to exercise WarningLatch's qualify/latch/acknowledge
     * cycle on a bench. A brief tap must not alarm; a sustained hold must
     * latch and stay latched after release.
     */
    bool pressure_drop_key_down();

} // namespace simulator_events
