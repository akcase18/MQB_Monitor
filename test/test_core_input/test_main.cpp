/**
 * @file test_main.cpp
 * @brief core/input: gesture recognition, the chord, and the action map.
 *
 * Every case here drives a synthetic timeline -- a loop variable standing
 * in for the clock -- rather than a real one, so an entire
 * press-hold-release sequence is a deterministic assertion instead of
 * something to verify with a finger and a stopwatch. That is the whole
 * reason ButtonGesture takes `now_ms` and holds no clock.
 *
 * Timings are the real ones from GestureTimings, and the expected instants
 * below are computed from them rather than written as literals, so moving
 * a threshold moves the tests with it instead of breaking them.
 *
 * Run with `pio test -e simulator`.
 */
#include <unity.h>

#include "action_map.hpp"
#include "button_gesture.hpp"
#include "button_input.hpp"

namespace
{
    const core::GestureTimings T;

    class FakeButtons : public IButtons
    {
        public:
            void begin() override
            {
            }

            uint8_t count() const override
            {
                return count_;
            }

            bool is_down(uint8_t idx) const override
            {
                return idx < 2 ? down_[idx] : false;
            }

            uint8_t count_   = 2;
            bool    down_[2] = { false, false };
    };

    struct Record
    {
            uint8_t           source;
            core::ButtonEvent event;
            uint32_t          at_ms;
    };

    /**
     * Drives ButtonInput over a timeline and keeps everything it emitted,
     * with the instant it happened -- the instants are half the point,
     * since "a short press is not delayed" is a claim about timing. */
    class Harness
    {
        public:
            static constexpr int MAX = 128;

            explicit Harness(FakeButtons &buttons)
                : buttons_(buttons)
                , input_(buttons)
            {
                input_.configure(T);
            }

            /**
             * Runs the clock up to but not including `to_ms`, polling
             * every `step_ms`.
             *
             * Exclusive on purpose: it leaves `now_` sitting exactly on
             * `to_ms`, so a set() straight afterwards is seen by the poll
             * at `to_ms` and the expected instants below are `to_ms` plus
             * a whole number of thresholds. An inclusive loop puts every
             * edge one millisecond late and every expectation off by one,
             * which reads like a state-machine bug and is not one.
             */
            void run_to(uint32_t to_ms, uint32_t step_ms = 1)
            {
                while (now_ < to_ms)
                {
                    poll_once();
                    now_ += step_ms;
                }
            }

            void poll_once()
            {
                input_.poll(now_);
                for (uint8_t i = 0; i < input_.event_count(); i++)
                {
                    if (count_ < MAX)
                    {
                        records_[count_].source = input_.event_at(i).source;
                        records_[count_].event  = input_.event_at(i).event;
                        records_[count_].at_ms  = now_;
                        count_++;
                    }
                }
            }

            /** Advances the clock with no polls at all -- a dropped
             * frame, not a coarse one. */
            void stall(uint32_t ms)
            {
                now_ += ms;
            }

            void set(uint8_t idx, bool down)
            {
                buttons_.down_[idx] = down;
            }

            /** Bounces `idx` for `ms` milliseconds, then settles to
             * `settle`. */
            void bounce(uint8_t idx, uint32_t ms, bool settle)
            {
                uint32_t end = now_ + ms;
                bool     lvl = true;
                while (now_ < end)
                {
                    buttons_.down_[idx] = lvl;
                    lvl                 = !lvl;
                    poll_once();
                    now_++;
                }
                buttons_.down_[idx] = settle;
            }

            int count() const
            {
                return count_;
            }

            const Record &at(int i) const
            {
                return records_[i];
            }

            int count_of(core::ButtonEvent event) const
            {
                int n = 0;
                for (int i = 0; i < count_; i++)
                {
                    n += records_[i].event == event ? 1 : 0;
                }
                return n;
            }

            int count_of(uint8_t source, core::ButtonEvent event) const
            {
                int n = 0;
                for (int i = 0; i < count_; i++)
                {
                    n += (records_[i].source == source
                          && records_[i].event == event)
                             ? 1
                             : 0;
                }
                return n;
            }

            /** Instant of the first matching event, or 0 if never. */
            uint32_t when(uint8_t source, core::ButtonEvent event) const
            {
                for (int i = 0; i < count_; i++)
                {
                    if (records_[i].source == source
                        && records_[i].event == event)
                    {
                        return records_[i].at_ms;
                    }
                }
                return 0;
            }

            core::ButtonInput &input()
            {
                return input_;
            }

            uint32_t now_ = 0;

        private:
            FakeButtons      &buttons_;
            core::ButtonInput input_;
            Record            records_[MAX];
            int               count_ = 0;
    };
} // namespace

/**
 * ==============================================================================
 * Debounce
 =============================================================================*/

void test_a_bouncing_contact_produces_exactly_one_press(void)
{
    FakeButtons buttons;
    buttons.count_ = 1;
    Harness h(buttons);

    h.run_to(100);

    /**
     * Ten milliseconds of chatter, well inside the debounce window, then
     * a clean hold. A machine watching raw edges would see five presses. */
    h.bounce(0, 10, true);
    h.run_to(300);

    h.set(0, false);
    h.run_to(1000);

    TEST_ASSERT_EQUAL_INT(1, h.count());
    TEST_ASSERT_EQUAL_INT(1, h.count_of(0, core::ButtonEvent::ShortPress));
}

void test_bounce_on_release_does_not_look_like_a_second_press(void)
{
    FakeButtons buttons;
    buttons.count_ = 1;
    Harness h(buttons);

    h.run_to(100);
    h.set(0, true);
    h.run_to(300);

    /**
     * Chatter on the way up is the one that produces phantom double
     * presses in a naive machine. */
    h.bounce(0, 10, false);
    h.run_to(1200);

    TEST_ASSERT_EQUAL_INT(1, h.count());
    TEST_ASSERT_EQUAL_INT(1, h.count_of(0, core::ButtonEvent::ShortPress));
    TEST_ASSERT_EQUAL_INT(0, h.count_of(0, core::ButtonEvent::DoublePress));
}

/**
 * ==============================================================================
 * Single vs double, at the window boundary
 =============================================================================*/

void test_a_second_press_inside_the_window_is_a_double(void)
{
    FakeButtons buttons;
    buttons.count_ = 1;
    Harness h(buttons);

    h.run_to(100);
    h.set(0, true);
    h.run_to(200);
    h.set(0, false);

    /**
     * Debounced release lands at 200 + debounce; the window closes
     * multi_gap after that. Land the second debounced press just inside. */
    uint32_t release_at = 200 + T.debounce_ms;
    uint32_t deadline   = release_at + T.multi_gap_ms;

    h.run_to(deadline - T.debounce_ms - 10);
    h.set(0, true);
    h.run_to(deadline + 100);
    h.set(0, false);
    h.run_to(deadline + 1000);

    TEST_ASSERT_EQUAL_INT(1, h.count_of(0, core::ButtonEvent::DoublePress));
    TEST_ASSERT_EQUAL_INT(0, h.count_of(0, core::ButtonEvent::ShortPress));
}

void test_a_second_press_past_the_window_is_two_singles(void)
{
    FakeButtons buttons;
    buttons.count_ = 1;
    Harness h(buttons);

    h.run_to(100);
    h.set(0, true);
    h.run_to(200);
    h.set(0, false);

    uint32_t release_at = 200 + T.debounce_ms;
    uint32_t deadline   = release_at + T.multi_gap_ms;

    /* Same sequence, second press landing just *after* the boundary. */
    h.run_to(deadline - T.debounce_ms + 10);
    h.set(0, true);
    h.run_to(deadline + 200);
    h.set(0, false);
    h.run_to(deadline + 1200);

    TEST_ASSERT_EQUAL_INT(2, h.count_of(0, core::ButtonEvent::ShortPress));
    TEST_ASSERT_EQUAL_INT(0, h.count_of(0, core::ButtonEvent::DoublePress));

    /**
     * And the first one fired exactly when the window closed, not
     * whenever the second press happened to arrive. */
    TEST_ASSERT_EQUAL_UINT32(deadline,
                             h.when(0, core::ButtonEvent::ShortPress));
}

/**
 * ==============================================================================
 * Holds
 =============================================================================*/

void test_hold_repeat_keeps_a_steady_cadence(void)
{
    FakeButtons buttons;
    buttons.count_ = 1;
    Harness h(buttons);

    h.run_to(100);
    h.set(0, true);
    h.run_to(2500);
    h.set(0, false);
    h.run_to(3000);

    uint32_t down_at = 100 + T.debounce_ms;
    TEST_ASSERT_EQUAL_UINT32(down_at + T.long_ms,
                             h.when(0, core::ButtonEvent::LongPress));
    TEST_ASSERT_EQUAL_INT(1, h.count_of(0, core::ButtonEvent::LongPress));
    TEST_ASSERT_EQUAL_INT(1, h.count_of(0, core::ButtonEvent::LongRelease));

    /* Every gap between consecutive repeats is exactly one interval. */
    uint32_t prev = 0;
    int      seen = 0;
    for (int i = 0; i < h.count(); i++)
    {
        if (h.at(i).event != core::ButtonEvent::HoldRepeat)
        {
            continue;
        }
        if (seen > 0)
        {
            TEST_ASSERT_EQUAL_UINT32(T.hold_repeat_ms, h.at(i).at_ms - prev);
        }
        prev = h.at(i).at_ms;
        seen++;
    }
    TEST_ASSERT_TRUE(seen >= 4);
}

void test_a_very_long_press_never_also_reads_as_a_click(void)
{
    FakeButtons buttons;
    buttons.count_ = 1;
    Harness h(buttons);

    h.run_to(100);
    h.set(0, true);
    h.run_to(100 + T.very_long_ms + 1000);
    h.set(0, false);
    h.run_to(100 + T.very_long_ms + 2000);

    /**
     * The classic bug in this kind of machine: a long hold that also
     * emits a click on the way out, so a 4-second press does two things. */
    TEST_ASSERT_EQUAL_INT(0, h.count_of(0, core::ButtonEvent::ShortPress));
    TEST_ASSERT_EQUAL_INT(0, h.count_of(0, core::ButtonEvent::DoublePress));

    /**
     * Each one-shot exactly once. LongPress *is* legitimately on the way
     * through -- it already fired at its threshold and something has
     * already happened on screen, so it is not retracted. That is why a
     * source maps either HoldRepeat or VeryLongPress, not two unrelated
     * actions to both. */
    TEST_ASSERT_EQUAL_INT(1, h.count_of(0, core::ButtonEvent::LongPress));
    TEST_ASSERT_EQUAL_INT(1, h.count_of(0, core::ButtonEvent::VeryLongPress));
    TEST_ASSERT_EQUAL_INT(1, h.count_of(0, core::ButtonEvent::LongRelease));

    uint32_t down_at = 100 + T.debounce_ms;
    TEST_ASSERT_EQUAL_UINT32(down_at + T.very_long_ms,
                             h.when(0, core::ButtonEvent::VeryLongPress));
}

/**
 * ==============================================================================
 * Chords
 =============================================================================*/

void test_pressing_both_together_emits_one_chord_and_no_long_presses(void)
{
    FakeButtons buttons;
    Harness     h(buttons);

    h.run_to(100);
    h.set(0, true);
    h.run_to(110);
    h.set(1, true); /* near-simultaneous, as a thumb and finger are */
    h.run_to(2000);
    h.set(0, false);
    h.set(1, false);
    h.run_to(2800);

    TEST_ASSERT_EQUAL_INT(
        1, h.count_of(core::INPUT_SOURCE_CHORD, core::ButtonEvent::LongPress));

    /* Neither button may also report its own long press... */
    TEST_ASSERT_EQUAL_INT(0, h.count_of(0, core::ButtonEvent::LongPress));
    TEST_ASSERT_EQUAL_INT(0, h.count_of(1, core::ButtonEvent::LongPress));

    /**
     * ...nor a click on release, which is what suppressing only the long
     * press would have left behind. */
    TEST_ASSERT_EQUAL_INT(0, h.count_of(core::ButtonEvent::ShortPress));
    TEST_ASSERT_EQUAL_INT(0, h.count_of(core::ButtonEvent::DoublePress));

    /* Letting go ends the chord, once. */
    TEST_ASSERT_EQUAL_INT(
        1,
        h.count_of(core::INPUT_SOURCE_CHORD, core::ButtonEvent::LongRelease));
    TEST_ASSERT_FALSE(h.input().chord_active());
}

void test_a_staggered_press_is_two_long_presses_and_no_chord(void)
{
    FakeButtons buttons;
    Harness     h(buttons);

    h.run_to(100);
    h.set(0, true);

    /* Second button arrives only after the first has already gone long. */
    h.run_to(100 + T.debounce_ms + T.long_ms + 200);
    h.set(1, true);
    h.run_to(h.now_ + T.debounce_ms + T.long_ms + 300);
    h.set(0, false);
    h.set(1, false);
    h.run_to(h.now_ + 800);

    TEST_ASSERT_EQUAL_INT(1, h.count_of(0, core::ButtonEvent::LongPress));
    TEST_ASSERT_EQUAL_INT(1, h.count_of(1, core::ButtonEvent::LongPress));
    TEST_ASSERT_EQUAL_INT(
        0, h.count_of(core::INPUT_SOURCE_CHORD, core::ButtonEvent::LongPress));
}

void test_a_held_chord_escalates_to_very_long(void)
{
    FakeButtons buttons;
    Harness     h(buttons);

    h.run_to(100);
    h.set(0, true);
    h.set(1, true);
    h.run_to(100 + T.very_long_ms + 1500);
    h.set(0, false);
    h.set(1, false);
    h.run_to(h.now_ + 500);

    /* Arm, then commit -- the two-step the factory-reset countdown needs. */
    TEST_ASSERT_EQUAL_INT(
        1, h.count_of(core::INPUT_SOURCE_CHORD, core::ButtonEvent::LongPress));
    TEST_ASSERT_EQUAL_INT(
        1,
        h.count_of(core::INPUT_SOURCE_CHORD, core::ButtonEvent::VeryLongPress));

    /**
     * Total hold to commit is very_long_ms from the press, not from
     * recognition -- "hold both for four seconds" is what a user counts. */
    uint32_t down_at = 100 + T.debounce_ms;
    TEST_ASSERT_EQUAL_UINT32(
        down_at + T.very_long_ms,
        h.when(core::INPUT_SOURCE_CHORD, core::ButtonEvent::VeryLongPress));
}

void test_a_short_press_is_not_delayed_by_having_a_second_button(void)
{
    /**
     * The point of recognising chords only at the long threshold: a single
     * short press must cost exactly what it costs on a one-button board.
     * Same sequence, two boards, same instant. */
    uint32_t timings[2];

    for (int i = 0; i < 2; i++)
    {
        FakeButtons buttons;
        buttons.count_ = static_cast<uint8_t>(i + 1);
        Harness h(buttons);

        h.run_to(100);
        h.set(0, true);
        h.run_to(200);
        h.set(0, false);
        h.run_to(1200);

        TEST_ASSERT_EQUAL_INT(1, h.count_of(0, core::ButtonEvent::ShortPress));
        timings[i] = h.when(0, core::ButtonEvent::ShortPress);
    }

    TEST_ASSERT_EQUAL_UINT32(timings[0], timings[1]);

    /**
     * And it is the multi-click window that costs the time, with no chord
     * window stacked on top of it. */
    TEST_ASSERT_EQUAL_UINT32(200 + T.debounce_ms + T.multi_gap_ms, timings[0]);
}

/**
 * ==============================================================================
 * Robustness
 =============================================================================*/

void test_gestures_survive_a_coarse_polling_cadence(void)
{
    /**
     * The real loop runs at tens of hertz, not a kilohertz, and it stalls
     * for a frame now and then. Nothing may depend on being polled finely. */
    FakeButtons buttons;
    buttons.count_ = 1;
    Harness h(buttons);

    h.run_to(100, 23);
    h.set(0, true);
    h.run_to(2200, 23);
    h.set(0, false);
    h.run_to(3000, 23);

    TEST_ASSERT_EQUAL_INT(1, h.count_of(0, core::ButtonEvent::LongPress));
    TEST_ASSERT_EQUAL_INT(1, h.count_of(0, core::ButtonEvent::LongRelease));
    TEST_ASSERT_EQUAL_INT(0, h.count_of(0, core::ButtonEvent::ShortPress));

    /**
     * Repeats are paced off the clock, so a coarse poll yields fewer of
     * them -- never a catch-up burst when the loop recovers. */
    int repeats = h.count_of(0, core::ButtonEvent::HoldRepeat);
    TEST_ASSERT_TRUE(repeats > 0);
    TEST_ASSERT_TRUE(repeats <= 10);
}

void test_a_dropped_frame_slows_the_ramp_instead_of_bursting(void)
{
    /**
     * A coarse *uniform* cadence is not the interesting case -- a stall
     * is. If the repeat schedule advanced by one interval per fire rather
     * than rebasing on now, the loop would pay back every missed interval
     * one-per-poll the instant it recovered, and a brightness ramp would
     * jump instead of ramping. */
    FakeButtons buttons;
    buttons.count_ = 1;
    Harness h(buttons);

    h.run_to(100);
    h.set(0, true);
    h.run_to(2000);

    /* Four repeat intervals' worth of dropped frames. */
    h.stall(T.hold_repeat_ms * 4);
    uint32_t resumed_at = h.now_;

    h.run_to(resumed_at + 1000);
    h.set(0, false);
    h.run_to(h.now_ + 500);

    /* Find the repeats from the stall onward and check their spacing. */
    uint32_t prev  = 0;
    int      after = 0;
    for (int i = 0; i < h.count(); i++)
    {
        if (h.at(i).event != core::ButtonEvent::HoldRepeat
            || h.at(i).at_ms < resumed_at)
        {
            continue;
        }
        if (after > 0)
        {
            TEST_ASSERT_EQUAL_UINT32(T.hold_repeat_ms, h.at(i).at_ms - prev);
        }
        prev = h.at(i).at_ms;
        after++;
    }
    TEST_ASSERT_TRUE(after >= 3);
}

void test_a_button_held_at_boot_is_ignored_until_released(void)
{
    /**
     * Otherwise a board that powers up with a stuck or pressed button
     * immediately performs whatever is mapped to it. */
    FakeButtons buttons;
    buttons.count_   = 1;
    buttons.down_[0] = true;

    Harness h(buttons);
    h.run_to(T.very_long_ms + 1000);

    TEST_ASSERT_EQUAL_INT(0, h.count());

    /* Released and pressed again, it behaves normally. */
    h.set(0, false);
    h.run_to(h.now_ + 500);
    h.set(0, true);
    h.run_to(h.now_ + 100);
    h.set(0, false);
    h.run_to(h.now_ + 800);

    TEST_ASSERT_EQUAL_INT(1, h.count_of(0, core::ButtonEvent::ShortPress));
}

void test_a_board_with_no_buttons_is_silent(void)
{
    /**
     * The MQB product supplies NullButtons. Polling must be free and
     * produce nothing rather than reading index 0 of an empty array. */
    FakeButtons buttons;
    buttons.count_   = 0;
    buttons.down_[0] = true;

    Harness h(buttons);
    h.run_to(5000);

    TEST_ASSERT_EQUAL_INT(0, h.count());
    TEST_ASSERT_EQUAL_UINT8(0, h.input().button_count());
}

/**
 * ==============================================================================
 * ActionMap
 =============================================================================*/

void test_default_map_puts_every_action_on_a_performable_gesture(void)
{
    core::ActionMap map;
    map.set_defaults(2);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::InputAction::CycleZoom),
        static_cast<int>(map.resolve(0, core::ButtonEvent::ShortPress)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::InputAction::RampBrightness),
        static_cast<int>(map.resolve(0, core::ButtonEvent::HoldRepeat)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::InputAction::ResetPeakHold),
        static_cast<int>(map.resolve(1, core::ButtonEvent::ShortPress)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::InputAction::ToggleWifiAp),
        static_cast<int>(map.resolve(1, core::ButtonEvent::LongPress)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::InputAction::FactoryResetArm),
        static_cast<int>(map.resolve(core::INPUT_SOURCE_CHORD,
                                     core::ButtonEvent::LongPress)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::InputAction::FactoryResetCommit),
        static_cast<int>(map.resolve(core::INPUT_SOURCE_CHORD,
                                     core::ButtonEvent::VeryLongPress)));

    /* An unmapped gesture is None, not the first binding in the table. */
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::InputAction::None),
        static_cast<int>(map.resolve(1, core::ButtonEvent::HoldRepeat)));
}

void test_a_latched_warning_pre_empts_whatever_is_mapped(void)
{
    core::ActionMap map;
    map.set_defaults(2);

    /**
     * Any discrete press on any source dismisses -- a driver reaching for
     * the dashboard because something lit up should not have to remember
     * which button does it. */
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::InputAction::AcknowledgeWarning),
        static_cast<int>(map.resolve(0, core::ButtonEvent::ShortPress, true)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::InputAction::AcknowledgeWarning),
        static_cast<int>(map.resolve(1, core::ButtonEvent::LongPress, true)));

    /**
     * But a repeat is not a press. Acknowledging once every 150ms while
     * somebody ramps the brightness is not a gesture. */
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::InputAction::RampBrightness),
        static_cast<int>(map.resolve(0, core::ButtonEvent::HoldRepeat, true)));
}

void test_rebinding_replaces_rather_than_shadows(void)
{
    core::ActionMap map;
    map.set_defaults(2);
    uint8_t before = map.binding_count();

    TEST_ASSERT_TRUE(map.bind(
        0, core::ButtonEvent::ShortPress, core::InputAction::ToggleLogging));

    /**
     * Remapping is the point of the map existing; a second entry for the
     * same gesture would leave resolve() depending on table order. */
    TEST_ASSERT_EQUAL_UINT8(before, map.binding_count());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::InputAction::ToggleLogging),
        static_cast<int>(map.resolve(0, core::ButtonEvent::ShortPress)));
}

void test_single_button_fallback_reshapes_and_cannot_reach_everything(void)
{
    core::ActionMap map;
    map.set_defaults(1);

    /**
     * Four performable gestures, so the map is re-shaped rather than
     * truncated: the ramp subsumes the day/night toggle, and Wi-Fi moves
     * to the gesture nobody hits by accident. */
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::InputAction::CycleZoom),
        static_cast<int>(map.resolve(0, core::ButtonEvent::ShortPress)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::InputAction::ToggleUnits),
        static_cast<int>(map.resolve(0, core::ButtonEvent::DoublePress)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::InputAction::ToggleWifiAp),
        static_cast<int>(map.resolve(0, core::ButtonEvent::VeryLongPress)));

    /**
     * And this is the honest part: peak-hold reset and factory reset are
     * simply not reachable with one button. Asserted rather than left
     * implicit so that "still reaches every action" is not quietly
     * believed of this configuration. */
    bool reset_reachable   = false;
    bool factory_reachable = false;
    for (uint8_t i = 0; i < map.binding_count(); i++)
    {
        core::InputAction a = map.binding_at(i).action;
        reset_reachable
            = reset_reachable || a == core::InputAction::ResetPeakHold;
        factory_reachable
            = factory_reachable || a == core::InputAction::FactoryResetCommit;
    }
    TEST_ASSERT_FALSE(reset_reachable);
    TEST_ASSERT_FALSE(factory_reachable);

    /* No chord bindings survive, since there is nothing to chord with. */
    for (uint8_t i = 0; i < map.binding_count(); i++)
    {
        TEST_ASSERT_NOT_EQUAL(core::INPUT_SOURCE_CHORD,
                              map.binding_at(i).source);
    }
}

void test_every_action_has_a_name(void)
{
    /**
     * The web form's gesture editor renders these, and a missing case in
     * the switch would show "unknown" to a user rather than fail a build. */
    for (int i = 0;
         i <= static_cast<int>(core::InputAction::FactoryResetCommit);
         i++)
    {
        const char *name
            = core::input_action_name(static_cast<core::InputAction>(i));
        TEST_ASSERT_NOT_NULL(name);
        TEST_ASSERT_TRUE(name[0] != '\0');
        TEST_ASSERT_TRUE(
            core::input_action_name(static_cast<core::InputAction>(i))
                != core::input_action_name(core::InputAction::None)
            || i == 0);
    }
}

void test_a_saved_map_round_trips_through_load(void)
{
    /**
     * core::DeviceConfig persists the binding table verbatim, so a remap
     * has to survive a power cycle without anything in between
     * understanding what a binding means. */
    core::ActionMap saved;
    saved.set_defaults(2);

    core::ActionBinding blob[core::ActionMap::MAX_BINDINGS];
    uint8_t             count = saved.binding_count();
    for (uint8_t i = 0; i < count; i++)
    {
        blob[i] = saved.binding_at(i);
    }

    core::ActionMap restored;
    restored.load(blob, count);

    TEST_ASSERT_EQUAL_UINT8(count, restored.binding_count());
    for (uint8_t i = 0; i < count; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(saved.binding_at(i).source,
                                restored.binding_at(i).source);
        TEST_ASSERT_EQUAL(saved.binding_at(i).event,
                          restored.binding_at(i).event);
        TEST_ASSERT_EQUAL(saved.binding_at(i).action,
                          restored.binding_at(i).action);
    }
}

void test_load_replaces_rather_than_appends(void)
{
    core::ActionMap map;
    map.set_defaults(2);
    TEST_ASSERT_TRUE(map.binding_count() > 1);

    const core::ActionBinding ONE[] = {
        { 0, core::ButtonEvent::ShortPress, core::InputAction::ToggleUnits },
    };
    map.load(ONE, 1);

    /**
     * Not merged on top of what was there -- a saved map is the whole
     * map. Leaving the old bindings underneath would resurrect a gesture
     * the owner had deliberately removed. */
    TEST_ASSERT_EQUAL_UINT8(1, map.binding_count());
    TEST_ASSERT_EQUAL(core::InputAction::ToggleUnits,
                      map.resolve(0, core::ButtonEvent::ShortPress));
    TEST_ASSERT_EQUAL(core::InputAction::None,
                      map.resolve(1, core::ButtonEvent::ShortPress));
}

void test_load_survives_a_malformed_table(void)
{
    /**
     * A blob is data, not a well-formed caller. Duplicates must collapse
     * rather than occupy two slots that then disagree, and a count past
     * the table's capacity must be dropped rather than overrun it. */
    core::ActionBinding blob[core::ActionMap::MAX_BINDINGS + 8];
    for (size_t i = 0; i < sizeof(blob) / sizeof(blob[0]); i++)
    {
        blob[i] = { 0,
                    core::ButtonEvent::ShortPress,
                    core::InputAction::CycleZoom };
    }
    blob[3].action = core::InputAction::ResetPeakHold;

    core::ActionMap map;
    map.load(blob, static_cast<uint8_t>(sizeof(blob) / sizeof(blob[0])));

    TEST_ASSERT_EQUAL_UINT8(1, map.binding_count());
    /* Last write wins, consistently with bind()'s own replace semantics. */
    TEST_ASSERT_EQUAL(core::InputAction::CycleZoom,
                      map.resolve(0, core::ButtonEvent::ShortPress));

    core::ActionMap empty;
    empty.load(nullptr, 5);
    TEST_ASSERT_EQUAL_UINT8(0, empty.binding_count());
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_a_bouncing_contact_produces_exactly_one_press);
    RUN_TEST(test_bounce_on_release_does_not_look_like_a_second_press);

    RUN_TEST(test_a_second_press_inside_the_window_is_a_double);
    RUN_TEST(test_a_second_press_past_the_window_is_two_singles);

    RUN_TEST(test_hold_repeat_keeps_a_steady_cadence);
    RUN_TEST(test_a_very_long_press_never_also_reads_as_a_click);

    RUN_TEST(test_pressing_both_together_emits_one_chord_and_no_long_presses);
    RUN_TEST(test_a_staggered_press_is_two_long_presses_and_no_chord);
    RUN_TEST(test_a_held_chord_escalates_to_very_long);
    RUN_TEST(test_a_short_press_is_not_delayed_by_having_a_second_button);

    RUN_TEST(test_gestures_survive_a_coarse_polling_cadence);
    RUN_TEST(test_a_dropped_frame_slows_the_ramp_instead_of_bursting);
    RUN_TEST(test_a_button_held_at_boot_is_ignored_until_released);
    RUN_TEST(test_a_board_with_no_buttons_is_silent);

    RUN_TEST(test_default_map_puts_every_action_on_a_performable_gesture);
    RUN_TEST(test_a_latched_warning_pre_empts_whatever_is_mapped);
    RUN_TEST(test_rebinding_replaces_rather_than_shadows);
    RUN_TEST(test_single_button_fallback_reshapes_and_cannot_reach_everything);
    RUN_TEST(test_every_action_has_a_name);
    RUN_TEST(test_a_saved_map_round_trips_through_load);
    RUN_TEST(test_load_replaces_rather_than_appends);
    RUN_TEST(test_load_survives_a_malformed_table);

    return UNITY_END();
}
