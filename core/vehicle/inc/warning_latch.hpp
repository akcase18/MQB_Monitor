/**
 * @file warning_latch.hpp
 * @brief Holds a warning on screen until somebody acknowledges it, and
 * refuses to raise one for a crossing too brief to be real.
 *
 * evaluate_warning() is stateless, which is the right shape for it and the
 * reason its tests are trivial -- but it means a 200 ms oil pressure dip on
 * a hard corner appears and vanishes between two refreshes, and the driver
 * never sees the one reading that mattered. This wraps it rather than
 * changing it: the evaluator stays pure and its tests stay untouched.
 *
 * ## Two opposing requirements, both real
 *
 * The build plan wants both of these, in different sections, and they pull
 * against each other:
 *
 * - **§8 Phase 6.4** -- a brief excursion must not be missed. Latch it.
 * - **§9.1** -- with no RPM input there is no honest low-oil-pressure
 *   threshold. One high enough to catch real loss at 3000 rpm alarms at
 *   every stoplight. So a brief dip must *not* alarm.
 *
 * They resolve because they are about different halves of the same event.
 * A crossing must persist for `qualify_ms` before it counts at all; once it
 * counts, it is held until acknowledged. Short enough not to be missed,
 * long enough not to be noise -- and `qualify_ms` of 0 gives exactly the
 * old latch-immediately behaviour, so this is opt-in.
 *
 * One qualify time for every signal rather than one per signal: a
 * temperature crossing is slow enough that qualifying it costs nothing, so
 * the extra configuration would buy nothing today.
 */
#pragma once

#include <stdint.h>

#include "signal_store.hpp"
#include "warning.hpp"

namespace core
{

    class WarningLatch
    {
        public:
            /**
             * How long a threshold crossing must persist before it raises
             * anything. 0 raises on the first sample past the line.
             */
            void set_qualify_ms(uint16_t ms);

            /**
             * Evaluates `value` and folds the result into the latch.
             *
             * @returns The state to *display*: a bound is active once it
             * has qualified, and stays active until acknowledged even
             * after the value comes back inside. A crossing that has not
             * yet qualified reports inactive -- that is the §9.1 half.
             */
            WarningState update(int                      idx,
                                const SignalValue       &value,
                                const WarningThresholds &thresholds,
                                uint32_t                 now_ms);

            /** Clears every held warning. If a condition is still present
             * it will qualify and latch again, which is correct -- an
             * acknowledgement dismisses the notice, not the fault. */
            void acknowledge();

            void acknowledge_at(int idx);

            /** True if anything is currently held. Drives the "any press
             * acknowledges" pre-emption in core::ActionMap. */
            bool any_held() const;

            bool held_at(int idx) const;

            /**
             * The same state update() returns, read back without
             * advancing anything.
             *
             * update() both *drives* the latch and *reports* it, which is
             * the right shape for the one caller that ticks it -- but a
             * view built only from the panels currently on screen would
             * then stop qualifying whatever is off screen. The zoom view
             * shows one panel of three, so that is not hypothetical.
             * Splitting the two lets the composition root tick every
             * signal on a timer and the presentation layer read whatever
             * it happens to be drawing.
             */
            WarningState state_at(int idx) const;

            /**
             * True when a bound is held but the value is no longer outside
             * it -- i.e. the warning is a memory of something that has
             * already passed. Worth showing differently from a condition
             * happening right now.
             */
            bool is_stale_at(int idx) const;

            /** Forgets everything, including qualification progress. */
            void reset();

        private:
            struct Entry
            {
                    uint32_t lo_since_ms = 0;
                    uint32_t hi_since_ms = 0;
                    bool     lo_crossing = false;
                    bool     hi_crossing = false;
                    bool     lo_held     = false;
                    bool     hi_held     = false;
            };

            Entry    entries_[SIGNAL_CATALOG_COUNT];
            uint16_t qualify_ms_ = 0;
    };

} // namespace core
