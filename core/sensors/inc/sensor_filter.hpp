/**
 * @file sensor_filter.hpp
 * @brief Median-of-three then exponential smoothing, per channel.
 *
 * Two stages because they defend against different things and neither
 * substitutes for the other. The median throws away single-sample outliers
 * outright -- an injector firing, an ignition event coupling into the
 * harness -- which an EMA would instead smear across the next several
 * seconds. The EMA then does the actual smoothing, so a gauge needle sits
 * still instead of flickering over the last digit.
 *
 * The time constant is in seconds and the update is dt-aware rather than
 * "alpha per sample". That matters because AnalogSignalSource visits
 * channels round-robin, so a channel's sample interval depends on how many
 * other channels are configured. With a per-sample alpha, adding a fourth
 * sensor would silently slow the other three down; here it does not change
 * their response at all.
 *
 * Pure and clock-free -- dt is passed in -- so a step response is a host
 * test rather than something to eyeball on a bench.
 */
#pragma once

#include <stdint.h>

namespace core
{

    class SensorFilter
    {
        public:
            /** @param tau_s EMA time constant, seconds. <= 0 passes samples
             * through unsmoothed (the median still applies). */
            void configure(float tau_s);

            /** Drops all history. Called when a channel's calibration
             * changes or when it comes back after a fault, so a stale
             * pre-fault value cannot bleed into the recovered reading. */
            void reset();

            /**
             * Folds one raw sample in.
             *
             * @param sample Conditioned value, engineering units.
             * @param dt_ms  Milliseconds since this channel's previous
             *               sample. 0 on the first sample of a run.
             *
             * @returns The current filtered value.
             */
            float update(float sample, uint32_t dt_ms);

            bool  has_value() const;
            float value() const;

        private:
            float   history_[3] = { 0.0f, 0.0f, 0.0f };
            uint8_t count_      = 0;

            float ema_    = 0.0f;
            bool  primed_ = false;
            float tau_s_  = 0.0f;
    };

} // namespace core
