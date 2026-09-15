#include "button_gesture.hpp"

namespace core
{

    void ButtonGesture::configure(const GestureTimings &timings)
    {
        timings_ = timings;
    }

    void ButtonGesture::suppress_until_release()
    {
        suppressed_     = true;
        waiting_multi_  = false;
        pending_clicks_ = 0;
    }

    bool ButtonGesture::is_down() const
    {
        return stable_down_;
    }

    bool ButtonGesture::long_fired() const
    {
        return long_fired_;
    }

    ButtonEvent ButtonGesture::update(bool raw_down, uint32_t now_ms)
    {
        /**
         * Adopt whatever the line is doing on the first call rather than
         * assuming it starts released -- a board powered up with a button
         * already held would otherwise register a press it never saw. */
        if (!initialized_)
        {
            initialized_   = true;
            raw_last_      = raw_down;
            stable_down_   = raw_down;
            raw_change_ms_ = now_ms;
            down_ms_       = now_ms;
            if (raw_down)
            {
                /**
                 * Held at boot: treat it as already suppressed so it
                 * cannot fire anything until it has been let go of. */
                suppressed_ = true;
            }
            return ButtonEvent::None;
        }

        /*-- Debounce the level -------------------------------------------*/
        if (raw_down != raw_last_)
        {
            raw_last_      = raw_down;
            raw_change_ms_ = now_ms;
        }

        bool was_down = stable_down_;
        if ((now_ms - raw_change_ms_) >= timings_.debounce_ms)
        {
            stable_down_ = raw_last_;
        }

        /*-- Edges --------------------------------------------------------*/
        if (stable_down_ && !was_down)
        {
            down_ms_         = now_ms;
            long_fired_      = false;
            very_long_fired_ = false;
            suppressed_      = false;
            waiting_multi_   = false;
            next_repeat_ms_
                = now_ms + timings_.long_ms + timings_.hold_repeat_ms;
            return ButtonEvent::None;
        }

        if (!stable_down_ && was_down)
        {
            if (suppressed_)
            {
                /* The chord took this hold. Nothing is owed on release. */
                suppressed_     = false;
                long_fired_     = false;
                pending_clicks_ = 0;
                return ButtonEvent::None;
            }

            if (long_fired_)
            {
                /**
                 * A long hold owes a LongRelease and nothing else. This is
                 * the branch that stops a 4-second hold from also reading
                 * as a click on the way out. */
                long_fired_     = false;
                pending_clicks_ = 0;
                return ButtonEvent::LongRelease;
            }

            pending_clicks_++;
            if (pending_clicks_ >= 2)
            {
                /**
                 * Fire on the second release rather than after another
                 * window -- there is no triple press to wait for, and the
                 * two-button map removed the reason one ever existed. */
                pending_clicks_ = 0;
                waiting_multi_  = false;
                return ButtonEvent::DoublePress;
            }

            release_ms_    = now_ms;
            waiting_multi_ = true;
            return ButtonEvent::None;
        }

        /*-- Held ---------------------------------------------------------*/
        if (stable_down_)
        {
            if (suppressed_)
            {
                return ButtonEvent::None;
            }

            uint32_t held = now_ms - down_ms_;

            if (!long_fired_ && held >= timings_.long_ms)
            {
                long_fired_ = true;
                /**
                 * A press that turns into a hold abandons any click it was
                 * part of; "click, then click-and-hold" is a hold. */
                pending_clicks_ = 0;
                waiting_multi_  = false;
                next_repeat_ms_ = now_ms + timings_.hold_repeat_ms;
                return ButtonEvent::LongPress;
            }

            if (long_fired_ && !very_long_fired_
                && held >= timings_.very_long_ms)
            {
                very_long_fired_ = true;
                return ButtonEvent::VeryLongPress;
            }

            if (long_fired_ && timings_.hold_repeat_ms > 0
                && static_cast<int32_t>(now_ms - next_repeat_ms_) >= 0)
            {
                /**
                 * Re-based on now rather than advanced by one interval, so
                 * a stalled frame slows the ramp instead of firing a burst
                 * of catch-up repeats the moment the loop recovers. */
                next_repeat_ms_ = now_ms + timings_.hold_repeat_ms;
                return ButtonEvent::HoldRepeat;
            }

            return ButtonEvent::None;
        }

        /*-- Released, waiting to see whether a second press arrives ------*/
        if (waiting_multi_ && (now_ms - release_ms_) >= timings_.multi_gap_ms)
        {
            waiting_multi_  = false;
            pending_clicks_ = 0;
            return ButtonEvent::ShortPress;
        }

        return ButtonEvent::None;
    }

} // namespace core
