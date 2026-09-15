/**
 * @file button_input.hpp
 * @brief Every button on the board plus the chord between them, polled as
 * one thing.
 *
 * Owns one ButtonGesture per button and turns the whole lot into a short
 * queue of (source, gesture) pairs the composition root drains. Reads
 * IButtons and nothing else; what a gesture *means* is action_map.hpp's
 * problem, and what an action *does* is the composition root's.
 *
 * ## Chords without a latency tax
 *
 * Both buttons together is a natural "confirm" or "escape". The obvious
 * implementation -- wait to see whether a second button joins before
 * acting on the first -- taxes *every* single press with a chord window,
 * on top of the multi-click window it already pays.
 *
 * So a chord is recognised **only at the long-press threshold**: it is
 * "the other button was already down when this one's LongPress came due,
 * and neither has gone long yet during this hold". There is no chord short
 * press, so a single short press needs no extra delay at all and the tax
 * for having a second button is exactly zero.
 *
 * The "neither has gone long yet" half is what separates the two cases
 * worth distinguishing:
 *
 * - Press both together: the first to reach its threshold finds the other
 *   down and unfired, so **one** chord is emitted and both buttons are
 *   suppressed for the rest of the hold.
 * - Press one, hold it past the threshold, then press the other: the first
 *   has already fired its own LongPress, so no chord forms and the second
 *   goes long independently. **Two** long presses, no chord.
 *
 * Suppression covers everything for the remainder of the hold, not only
 * LongPress -- otherwise letting go of a chord would also read as two
 * short presses.
 *
 * ## Not an lv_indev
 *
 * These drive app logic directly. No lv_group, no focus traversal, no
 * LVGL input plumbing. That is the largest simplification a touchless
 * design buys, and it holds only as long as buttons never navigate.
 */
#pragma once

#include <stdint.h>

#include "button_gesture.hpp"
#include "platform.hpp"

namespace core
{

    /** `InputEvent::source` for the both-buttons chord. Deliberately not a
     * button index, so a binding cannot collide with a real button. */
    constexpr uint8_t INPUT_SOURCE_CHORD = 0xFF;

    struct InputEvent
    {
            uint8_t     source; /* button index, or INPUT_SOURCE_CHORD */
            ButtonEvent event;
    };

    class ButtonInput
    {
        public:
            /** Two is what the gauge product routes. A third would need a
             * chord policy for which pair counts, which is not worth
             * inventing before a board has one. */
            static constexpr uint8_t MAX_BUTTONS = 2;

            /** One per button plus one chord is the true ceiling; four
             * leaves room without being a number anyone has to justify. */
            static constexpr uint8_t MAX_EVENTS_PER_POLL = 4;

            explicit ButtonInput(IButtons &buttons);

            void configure(const GestureTimings &timings);

            /**
             * Samples every button and refills the event queue, discarding
             * whatever the previous poll left. Call once per main-loop
             * iteration.
             *
             * @param now_ms Monotonic milliseconds, from the composition
             *               root's clock -- same shape as
             *               core::ISignalSource::tick().
             */
            void poll(uint32_t now_ms);

            uint8_t           event_count() const;
            const InputEvent &event_at(uint8_t index) const;

            /** How many buttons are actually being watched: the board's
             * count, clamped to MAX_BUTTONS. Zero on a touchscreen board,
             * which makes poll() a no-op. */
            uint8_t button_count() const;

            /** True while a chord is being held. The composition root uses
             * it to keep a confirmation countdown on screen. */
            bool chord_active() const;

        private:
            void push(uint8_t source, ButtonEvent event);
            bool try_start_chord(uint8_t triggering_button, uint32_t now_ms);
            void service_chord(uint32_t now_ms);

            IButtons &buttons_;

            ButtonGesture  gestures_[MAX_BUTTONS];
            GestureTimings timings_;
            uint8_t        count_ = 0;

            bool     chord_active_          = false;
            bool     chord_very_long_fired_ = false;
            uint32_t chord_start_ms_        = 0;
            uint32_t chord_next_repeat_ms_  = 0;

            InputEvent queue_[MAX_EVENTS_PER_POLL];
            uint8_t    queued_ = 0;
    };

} // namespace core
