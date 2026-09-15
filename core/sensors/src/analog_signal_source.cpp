#include "analog_signal_source.hpp"

#include "signal_catalog.hpp"

namespace core
{

    AnalogSignalSource::AnalogSignalSource(IAnalogInput &adc,
                                           SignalStore  &store)
        : adc_(adc)
        , store_(store)
    {
        for (uint8_t i = 0; i < MAX_CHANNELS; i++)
        {
            cals_[i]          = SensorCal {};
            cals_[i].kind     = SensorKind::Unconfigured;
            catalog_index_[i] = -1;
        }
    }

    void AnalogSignalSource::configure(const SensorCal *cals, uint8_t count)
    {
        if (cals == nullptr)
        {
            count = 0;
        }
        if (count > MAX_CHANNELS)
        {
            count = MAX_CHANNELS;
        }

        count_                  = count;
        rail_ch_                = -1;
        cursor_                 = 0;
        any_ratiometric_active_ = false;

        for (uint8_t i = 0; i < MAX_CHANNELS; i++)
        {
            states_[i] = ChannelState {};
            filters_[i].reset();

            if (i >= count_)
            {
                cals_[i]          = SensorCal {};
                cals_[i].kind     = SensorKind::Unconfigured;
                catalog_index_[i] = -1;
                continue;
            }

            cals_[i] = cals[i];
            filters_[i].configure(cals_[i].filter_tau_s);

            states_[i].configured = cals_[i].kind != SensorKind::Unconfigured;

            catalog_index_[i] = cals_[i].signal_id == SENSOR_SIGNAL_NONE
                                    ? -1
                                    : static_cast<int8_t>(signal_catalog_find(
                                          cals_[i].signal_id));

            if (cals_[i].kind == SensorKind::RailSense && rail_ch_ < 0)
            {
                rail_ch_ = static_cast<int8_t>(i);
            }
        }

        /**
         * The active set was resolved against the old table, so re-resolve
         * it. Cheap, and it means configure()/set_active() can arrive in
         * either order at boot without one of them being silently lost. */
        uint16_t ids[ISignalSource::MAX_ACTIVE_SIGNALS];
        uint8_t  n = active_count_;
        for (uint8_t i = 0; i < n; i++)
        {
            ids[i] = active_ids_[i];
        }
        active_count_ = 0;
        set_active(ids, n);
    }

    void AnalogSignalSource::set_sample_interval_ms(uint32_t ms)
    {
        sample_interval_ms_ = ms > 0 ? ms : 1;
    }

    void AnalogSignalSource::set_active(const uint16_t *signal_ids,
                                        uint8_t         count)
    {
        if (signal_ids == nullptr)
        {
            count = 0;
        }
        if (count > ISignalSource::MAX_ACTIVE_SIGNALS)
        {
            count = ISignalSource::MAX_ACTIVE_SIGNALS;
        }

        /**
         * The interface promises an unchanged list is a no-op, so callers
         * can invoke this on a timer as a backstop. Re-marking channels
         * would also drop every filter, which at 2 s of temperature
         * smoothing is very visible. */
        if (count == active_count_)
        {
            bool same = true;
            for (uint8_t i = 0; i < count; i++)
            {
                if (active_ids_[i] != signal_ids[i])
                {
                    same = false;
                    break;
                }
            }
            if (same)
            {
                return;
            }
        }

        active_count_ = count;
        for (uint8_t i = 0; i < count; i++)
        {
            active_ids_[i] = signal_ids[i];
        }

        any_ratiometric_active_ = false;

        for (uint8_t ch = 0; ch < MAX_CHANNELS; ch++)
        {
            bool active = false;
            for (uint8_t i = 0; i < active_count_ && !active; i++)
            {
                active = states_[ch].configured
                         && cals_[ch].signal_id != SENSOR_SIGNAL_NONE
                         && cals_[ch].signal_id == active_ids_[i];
            }

            if (states_[ch].active && !active)
            {
                /**
                 * Going dark: drop the history so coming back does not
                 * blend a value from before the panel was reassigned. */
                filters_[ch].reset();
            }
            states_[ch].active = active;

            if (active && cals_[ch].kind == SensorKind::LinearVoltage
                && cals_[ch].linear.ratiometric)
            {
                any_ratiometric_active_ = true;
            }
        }
    }

    bool AnalogSignalSource::channel_wanted(uint8_t index) const
    {
        if (!states_[index].configured)
        {
            return false;
        }
        if (states_[index].active)
        {
            return true;
        }

        /**
         * The rail is not displayed, so it is never "active" -- but three
         * good sensors read wrong together without it, so it is sampled
         * whenever anything depending on it is. */
        return cals_[index].kind == SensorKind::RailSense
               && any_ratiometric_active_;
    }

    uint8_t AnalogSignalSource::next_channel(uint8_t from) const
    {
        for (uint8_t step = 0; step < count_; step++)
        {
            uint8_t idx = static_cast<uint8_t>((from + step) % count_);
            if (channel_wanted(idx))
            {
                return idx;
            }
        }
        return MAX_CHANNELS; /* nothing to do */
    }

    void AnalogSignalSource::tick(uint32_t now_ms)
    {
        if (count_ == 0)
        {
            return;
        }

        if (!slot_started_)
        {
            slot_started_ = true;
            last_slot_ms_ = now_ms;
        }
        else if ((now_ms - last_slot_ms_) < sample_interval_ms_)
        {
            return;
        }
        last_slot_ms_ = now_ms;

        uint8_t ch = next_channel(cursor_);
        if (ch >= MAX_CHANNELS)
        {
            return;
        }
        cursor_ = static_cast<uint8_t>((ch + 1) % count_);

        sample_channel(ch, now_ms);
    }

    void AnalogSignalSource::sample_channel(uint8_t index, uint32_t now_ms)
    {
        ChannelState &state = states_[index];

        /**
         * The ADC channel comes from the calibration, not from the table
         * slot, so which physical input feeds which gauge stays
         * configuration rather than an ordering convention. */
        uint32_t microvolts = 0;
        state.read_ok = adc_.read_microvolts(cals_[index].channel, microvolts);

        if (!state.read_ok)
        {
            /**
             * The converter itself failed -- a bus error or a timeout.
             * Distinct from an implausible reading, and handled the same
             * way for the same reason: the value is unknown, so nothing
             * is written. */
            state.status = SensorStatus::BadCal;
            filters_[index].reset();
            state.last_sample_ms = now_ms;
            return;
        }

        state.volts = static_cast<float>(microvolts) / 1000000.0f;

        float         rail    = rail_volts();
        SensorReading reading = sensor_convert(cals_[index], state.volts, rail);

        state.raw_value = reading.value;
        state.status    = reading.status;

        /**
         * Asking the filter whether it is primed, rather than treating a
         * zero timestamp as "first sample" -- now_ms legitimately is 0 for
         * the first millisecond after boot. */
        uint32_t dt_ms
            = filters_[index].has_value() ? (now_ms - state.last_sample_ms) : 0;
        state.last_sample_ms = now_ms;

        if (!sensor_reading_is_usable(reading.status))
        {
            /**
             * No write. See the header: has_data stays false, which blanks
             * the panel and suppresses the warning through the path that
             * already handles "no response yet". */
            filters_[index].reset();
            return;
        }

        state.value = filters_[index].update(reading.value, dt_ms);

        if (catalog_index_[index] >= 0)
        {
            store_.set_at(catalog_index_[index], state.value, now_ms);
        }
    }

    float AnalogSignalSource::rail_volts() const
    {
        if (rail_ch_ < 0)
        {
            return 0.0f;
        }

        const ChannelState &state = states_[rail_ch_];
        if (!sensor_reading_is_usable(state.status))
        {
            /**
             * An implausible rail reading is worse than none: correcting
             * against it would move every ratiometric sensor at once. Fall
             * back to the configured nominal supply by reporting nothing. */
            return 0.0f;
        }
        return state.value;
    }

    uint8_t AnalogSignalSource::channel_count() const
    {
        return count_;
    }

    const AnalogSignalSource::ChannelState &AnalogSignalSource::channel_state(
        uint8_t index) const
    {
        static const ChannelState s_none;
        if (index >= MAX_CHANNELS)
        {
            return s_none;
        }
        return states_[index];
    }

    const SensorCal &AnalogSignalSource::channel_cal(uint8_t index) const
    {
        static const SensorCal s_none {};
        if (index >= MAX_CHANNELS)
        {
            return s_none;
        }
        return cals_[index];
    }

} // namespace core
