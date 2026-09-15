/**
 * @file button_gesture.hpp
 * @brief One physical button's worth of state machine: raw level in,
 * gestures out.
 *
 * Pure -- no clock member, no hardware, no knowledge of what any gesture
 * is *for*. `now_ms` is passed in the same way core::ISignalSource::tick()
 * takes it, which means an entire press-hold-release sequence is a host
 * test with a loop variable rather than something to verify with a finger
 * and a stopwatch.
 *
 * One instance per button. They know nothing about each other; the only
 * interaction between two buttons is the chord recogniser in
 * button_input.hpp, which reaches in through suppress_until_release().
 *
 * ## Ordering guarantee
 *
 * A single hold produces gestures in exactly this order, and each of the
 * one-shot ones at most once:
 *
 *     LongPress -> HoldRepeat* -> VeryLongPress -> LongRelease
 *
 * and a hold that goes long **never** also produces ShortPress or
 * DoublePress on release. That is the classic bug in this kind of machine
 * and the reason LongRelease exists as its own event rather than being
 * inferred from a release.
 *
 * Note that LongPress is genuinely on the way to VeryLongPress -- it is
 * not suppressed retroactively, because it already fired at its threshold
 * and something has already happened on screen. So a source should map
 * *either* HoldRepeat (a continuous ramp) *or* VeryLongPress (a deliberate
 * escalation), not two unrelated actions to both. The intended shape for
 * the escalating case is that LongPress starts something visible and
 * VeryLongPress commits it -- see the factory-reset countdown in the build
 * plan's action map.
 */
#pragma once

#include <stdint.h>

namespace core
{

    enum class ButtonEvent : uint8_t
    {
        None = 0,

        /** Confirmed only once the multi-click window closes, so it is
         * inherently ~MULTI_GAP_MS late. Anything needing immediacy
         * belongs on LongPress, which fires at its threshold. */
        ShortPress,

        /** Emitted on the *second* release, not after a further window --
         * there is no triple press to wait for. */
        DoublePress,

        /** Fires AT the threshold, while still held. */
        LongPress,

        /** Repeats while held. A brightness ramp, not a stepper. */
        HoldRepeat,

        LongRelease,

        /** For actions that should be hard to do by accident. */
        VeryLongPress
    };

    /**
     * Default timings. Held in a struct rather than as bare constants so a
     * test can drive a compressed timeline and so these become editable
     * from the web form later without touching this file.
     */
    struct GestureTimings
    {
            uint16_t debounce_ms    = 25;
            uint16_t multi_gap_ms   = 300;
            uint16_t long_ms        = 700;
            uint16_t hold_repeat_ms = 150;
            uint16_t very_long_ms   = 4000;
    };

    class ButtonGesture
    {
        public:
            void configure(const GestureTimings &timings);

            /**
             * Advances the machine.
             *
             * @param raw_down Unfiltered level straight off IButtons.
             * @param now_ms   Monotonic milliseconds.
             *
             * @returns At most one gesture per call, or ButtonEvent::None.
             * Call every main-loop iteration; the machine paces itself off
             * the clock rather than off call count, so a stalled frame
             * costs a slower ramp and never a lost press.
             */
            ButtonEvent update(bool raw_down, uint32_t now_ms);

            /**
             * Abandons the current hold: no further gesture is emitted
             * until the button is physically released and pressed again.
             *
             * The chord recogniser calls this on both buttons the moment a
             * chord is recognised. It suppresses *everything* for the rest
             * of the hold, not only LongPress -- otherwise letting go of a
             * chord would also read as two short presses, which is a
             * worse bug than the one suppression exists to fix.
             */
            void suppress_until_release();

            /** Debounced level, for the chord recogniser. */
            bool is_down() const;

            /** True once this hold has passed the long threshold. The
             * chord recogniser uses it to refuse a chord that formed too
             * late -- see button_input.hpp. */
            bool long_fired() const;

        private:
            GestureTimings timings_;

            /**
             * Level debouncing: the input must hold still for debounce_ms
             * before it counts as a change. A contact that bounces for
             * 5ms produces one press, not four. */
            bool     initialized_   = false;
            bool     raw_last_      = false;
            uint32_t raw_change_ms_ = 0;
            bool     stable_down_   = false;

            uint32_t down_ms_        = 0;
            uint32_t release_ms_     = 0;
            uint32_t next_repeat_ms_ = 0;

            uint8_t pending_clicks_  = 0;
            bool    long_fired_      = false;
            bool    very_long_fired_ = false;
            bool    suppressed_      = false;
            bool    waiting_multi_   = false;
    };

} // namespace core
