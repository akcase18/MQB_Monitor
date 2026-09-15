#include "button_input.hpp"

namespace core
{

    ButtonInput::ButtonInput(IButtons &buttons)
        : buttons_(buttons)
    {
        count_ = buttons_.count();
        if (count_ > MAX_BUTTONS)
        {
            count_ = MAX_BUTTONS;
        }
    }

    void ButtonInput::configure(const GestureTimings &timings)
    {
        timings_ = timings;
        for (uint8_t i = 0; i < MAX_BUTTONS; i++)
        {
            gestures_[i].configure(timings);
        }
    }

    uint8_t ButtonInput::button_count() const
    {
        return count_;
    }

    bool ButtonInput::chord_active() const
    {
        return chord_active_;
    }

    uint8_t ButtonInput::event_count() const
    {
        return queued_;
    }

    const InputEvent &ButtonInput::event_at(uint8_t index) const
    {
        static const InputEvent s_none { 0, ButtonEvent::None };
        if (index >= queued_)
        {
            return s_none;
        }
        return queue_[index];
    }

    void ButtonInput::push(uint8_t source, ButtonEvent event)
    {
        if (event == ButtonEvent::None || queued_ >= MAX_EVENTS_PER_POLL)
        {
            return;
        }
        queue_[queued_].source = source;
        queue_[queued_].event  = event;
        queued_++;
    }

    bool ButtonInput::try_start_chord(uint8_t  triggering_button,
                                      uint32_t now_ms)
    {
        if (count_ < 2 || chord_active_)
        {
            return false;
        }

        /**
         * The other button must already be down, and neither may have gone
         * long during this hold. The second half is what makes "press one,
         * hold it, then press the other" two independent long presses
         * rather than a chord formed a second late. */
        for (uint8_t i = 0; i < count_; i++)
        {
            if (i == triggering_button)
            {
                continue;
            }
            if (!gestures_[i].is_down() || gestures_[i].long_fired())
            {
                return false;
            }
        }

        chord_active_          = true;
        chord_very_long_fired_ = false;
        chord_start_ms_        = now_ms;
        chord_next_repeat_ms_  = now_ms + timings_.hold_repeat_ms;

        for (uint8_t i = 0; i < count_; i++)
        {
            gestures_[i].suppress_until_release();
        }

        return true;
    }

    void ButtonInput::service_chord(uint32_t now_ms)
    {
        bool all_down = true;
        for (uint8_t i = 0; i < count_; i++)
        {
            all_down = all_down && gestures_[i].is_down();
        }

        if (!all_down)
        {
            chord_active_ = false;
            push(INPUT_SOURCE_CHORD, ButtonEvent::LongRelease);
            return;
        }

        /**
         * Measured so that the total hold is very_long_ms from the first
         * press, not from recognition -- recognition already cost
         * long_ms, and "hold both for four seconds" is what a user is
         * actually counting. */
        uint32_t held_since_chord = now_ms - chord_start_ms_;
        if (!chord_very_long_fired_ && timings_.very_long_ms > timings_.long_ms
            && held_since_chord >= static_cast<uint32_t>(timings_.very_long_ms
                                                         - timings_.long_ms))
        {
            chord_very_long_fired_ = true;
            push(INPUT_SOURCE_CHORD, ButtonEvent::VeryLongPress);
            return;
        }

        if (timings_.hold_repeat_ms > 0
            && static_cast<int32_t>(now_ms - chord_next_repeat_ms_) >= 0)
        {
            chord_next_repeat_ms_ = now_ms + timings_.hold_repeat_ms;
            push(INPUT_SOURCE_CHORD, ButtonEvent::HoldRepeat);
        }
    }

    void ButtonInput::poll(uint32_t now_ms)
    {
        queued_ = 0;

        if (count_ == 0)
        {
            return;
        }

        for (uint8_t i = 0; i < count_; i++)
        {
            ButtonEvent event
                = gestures_[i].update(buttons_.is_down(i), now_ms);

            /**
             * Intercept rather than pre-empt: the gesture machine is what
             * knows when a long press is due, so the chord is decided at
             * the instant one comes out. Suppression then reaches the
             * other button before its own update() is called this pass. */
            if (event == ButtonEvent::LongPress && try_start_chord(i, now_ms))
            {
                push(INPUT_SOURCE_CHORD, ButtonEvent::LongPress);
                continue;
            }

            push(i, event);
        }

        if (chord_active_)
        {
            service_chord(now_ms);
        }
    }

} // namespace core
