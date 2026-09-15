/**
 * @file analog_signal_source.hpp
 * @brief core::ISignalSource over an ADC: read a channel, condition it,
 * decide whether to believe it, write it.
 *
 * The analog counterpart to core/can's UdsSignalSource, and deliberately
 * the smallest file in the module -- everything difficult is pure maths in
 * conditioning.hpp, where it can be tested without an ADC.
 *
 * **Channels are visited round-robin, one per sample slot.** tick() runs in
 * the same cooperative loop as lv_timer_handler(), so it does at most one
 * I2C conversion per call rather than sweeping every channel and handing
 * back a dropped frame. A channel's own sample interval is therefore
 * `sample_interval_ms * configured_channels`, which is why SensorFilter
 * takes a dt instead of a fixed per-sample alpha: adding a fourth sensor
 * must not silently slow the other three.
 *
 * **A reading that fails its plausibility check produces no write.** Not a
 * zero, not a stale value, not a flag for somebody downstream to remember
 * to check -- nothing. `has_data` stays false, the panel blanks and the
 * warning is suppressed, all through machinery that already existed for
 * "the ECU has not answered yet". That is the whole reason the store's
 * write path was left as the only write path.
 */
#pragma once

#include <stdint.h>

#include "conditioning.hpp"
#include "platform.hpp"
#include "sensor_cal.hpp"
#include "sensor_filter.hpp"
#include "signal_source.hpp"
#include "signal_store.hpp"

namespace core
{

    class AnalogSignalSource : public ISignalSource
    {
        public:
            /** Taken from core::SENSOR_MAX_CHANNELS rather than declared
             * again: the persisted schema stores that many calibrations,
             * and a source that could hold more would accept a table it
             * could never save. */
            static constexpr uint8_t MAX_CHANNELS = SENSOR_MAX_CHANNELS;

            /** Per-slot, not per-channel: with four channels configured
             * this samples each one about every 48 ms, comfortably ahead
             * of a 20 Hz display. */
            static constexpr uint32_t DEFAULT_SAMPLE_INTERVAL_MS = 12;

            /** What one channel last did. The web form's live calibration
             * readout is built from these -- raw volts and the conditioned
             * number side by side is what makes a two-point capture
             * possible without a multimeter. */
            struct ChannelState
            {
                    bool         configured = false;
                    bool         active     = false;
                    float        volts      = 0.0f; /* at the ADC pin */
                    float        raw_value = 0.0f; /* conditioned, unfiltered */
                    float        value     = 0.0f; /* what was written */
                    bool         read_ok   = false;
                    SensorStatus status    = SensorStatus::Unconfigured;
                    uint32_t     last_sample_ms = 0;
            };

            AnalogSignalSource(IAnalogInput &adc, SignalStore &store);

            /**
             * Replaces the channel table. Copies -- `cals` need not
             * outlive the call, so the config blob can be a local.
             *
             * Resets every filter: a calibration change means the previous
             * samples were measuring something else.
             */
            void configure(const SensorCal *cals, uint8_t count);

            /** @param ms Slot interval; clamped to at least 1. */
            void set_sample_interval_ms(uint32_t ms);

            void set_active(const uint16_t *signal_ids, uint8_t count) override;

            void tick(uint32_t now_ms) override;

            uint8_t             channel_count() const;
            const ChannelState &channel_state(uint8_t index) const;
            const SensorCal    &channel_cal(uint8_t index) const;

            /** Last believed supply voltage, or 0 if no rail-sense channel
             * is configured or its reading is currently implausible. */
            float rail_volts() const;

        private:
            void    sample_channel(uint8_t index, uint32_t now_ms);
            bool    channel_wanted(uint8_t index) const;
            uint8_t next_channel(uint8_t from) const;

            IAnalogInput &adc_;
            SignalStore  &store_;

            SensorCal    cals_[MAX_CHANNELS];
            ChannelState states_[MAX_CHANNELS];
            SensorFilter filters_[MAX_CHANNELS];

            /**
             * Resolved once per configure()/set_active() rather than per
             * sample: signal_catalog_find() is a linear scan of the whole
             * catalog, and this runs at 80 Hz. */
            int8_t catalog_index_[MAX_CHANNELS];

            uint8_t count_                  = 0;
            uint8_t cursor_                 = 0;
            int8_t  rail_ch_                = -1;
            bool    any_ratiometric_active_ = false;

            uint16_t active_ids_[ISignalSource::MAX_ACTIVE_SIGNALS];
            uint8_t  active_count_ = 0;

            uint32_t sample_interval_ms_ = DEFAULT_SAMPLE_INTERVAL_MS;
            uint32_t last_slot_ms_       = 0;
            bool     slot_started_       = false;
    };

} // namespace core
