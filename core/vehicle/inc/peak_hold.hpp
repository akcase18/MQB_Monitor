/**
 * @file peak_hold.hpp
 * @brief Highest and lowest each signal has reached since the last reset.
 *
 * Most of why somebody fits gauges at all: the number that matters is
 * rarely the one on screen when you happen to look down. Max oil temp and
 * max coolant temp over a drive, minimum oil pressure over a drive -- those
 * are the readings worth knowing, and they are exactly the ones a live
 * display cannot give you.
 *
 * Deliberately alongside SignalStore rather than inside it. The store means
 * "this is the current value"; conflating that with "and here is the worst
 * it has been" would give its single write path two jobs, and this is
 * resettable state whose lifetime is a drive rather than a reading.
 *
 * Both bounds are tracked for every signal even though any given gauge
 * shows only one of them -- which one is interesting depends on the signal
 * (high for a temperature, low for a pressure), and that is a presentation
 * decision, not a reason to throw half the data away while sampling.
 */
#pragma once

#include <stdint.h>

#include "signal_store.hpp"

namespace core
{

    class PeakHold
    {
        public:
            /**
             * Folds every signal's current value in.
             *
             * **Only signals whose `has_data` is set are sampled.** That is
             * the load-bearing line: an analog channel that has failed its
             * plausibility check declines to write, so its `has_data` stays
             * false -- and a peak tracker that sampled it anyway would pull
             * the minimum to zero and leave a permanent "0.0 bar minimum oil
             * pressure" on screen, sourced entirely from a disconnected
             * wire.
             */
            void sample(const SignalStore &store);

            /** Forgets everything. The mapped reset gesture, and boot. */
            void reset();

            /** False until this signal has been sampled at least once, so
             * an untouched entry is never displayed as 0.0. */
            bool has_data_at(int idx) const;

            float min_at(int idx) const;
            float max_at(int idx) const;

            /** Id-keyed forms, for callers that do not already hold an
             * index. */
            bool  has_data(uint16_t id) const;
            float min(uint16_t id) const;
            float max(uint16_t id) const;

        private:
            struct Entry
            {
                    float min      = 0.0f;
                    float max      = 0.0f;
                    bool  has_data = false;
            };

            Entry entries_[SIGNAL_CATALOG_COUNT];
    };

} // namespace core
