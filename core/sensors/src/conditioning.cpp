#include "conditioning.hpp"

#include <math.h>

namespace core
{

    namespace
    {
        constexpr float KELVIN_0C  = 273.15f;
        constexpr float KELVIN_25C = 298.15f;

        /**
         * Below this the pull-up divider's arithmetic is meaningless and
         * the leg is shorted, not cold. A real short reads single-digit
         * millivolts, which is thousands of times more resistance than
         * this, so nothing legitimate is caught by it. */
        constexpr float MIN_PLAUSIBLE_OHMS = 0.5f;

        /**
         * A node within this fraction of the supply is an open leg. The
         * hottest an NTC ever gets still leaves it far from the rail. */
        constexpr float OPEN_CIRCUIT_FRACTION = 1e-4f;

        /**
         * Stands in for "the model says a temperature at or below absolute
         * zero", which an NTC curve produces when resistance runs far past
         * the bottom of what it was fitted for. Deliberately a number
         * rather than a NAN: it is out of every plausible band, so it
         * reports as AboveRange and no write happens -- whereas a NAN
         * compares false against both bounds and would sail through. */
        constexpr float ABSURDLY_HOT_C = 9999.0f;

        float temp_from_inverse_kelvin(float inv_t)
        {
            if (!(inv_t > 0.0f))
            {
                return ABSURDLY_HOT_C;
            }
            return (1.0f / inv_t) - KELVIN_0C;
        }

        /**
         * Widens [lo, hi] by `margin` of its own width and reports which
         * side, if either, `v` falls off. Shared by every kind so the
         * band rule is written once. */
        SensorStatus band_check(float v, float lo, float hi, float margin)
        {
            if (!(hi > lo))
            {
                return SensorStatus::BadCal;
            }

            float slack = (hi - lo) * (margin > 0.0f ? margin : 0.0f);

            if (v < (lo - slack))
            {
                return SensorStatus::BelowRange;
            }
            if (v > (hi + slack))
            {
                return SensorStatus::AboveRange;
            }
            return SensorStatus::Ok;
        }

        void ordered(float a, float b, float &lo, float &hi)
        {
            lo = a < b ? a : b;
            hi = a < b ? b : a;
        }
    } // namespace

    const char *sensor_status_text(SensorStatus status)
    {
        switch (status)
        {
            case SensorStatus::Ok:
                return "ok";
            case SensorStatus::Unconfigured:
                return "not configured";
            case SensorStatus::BadCal:
                return "bad calibration";
            case SensorStatus::OpenCircuit:
                return "open circuit";
            case SensorStatus::ShortCircuit:
                return "short circuit";
            case SensorStatus::BelowRange:
                return "below range";
            case SensorStatus::AboveRange:
                return "above range";
        }
        return "unknown";
    }

    float divider_source_volts(const Divider &divider, float v_adc)
    {
        if (divider.r_top <= 0.0f || divider.r_bottom <= 0.0f)
        {
            return v_adc;
        }
        return v_adc * ((divider.r_top + divider.r_bottom) / divider.r_bottom);
    }

    float linear_apply(const LinearCal &cal, float v_source)
    {
        float dv = cal.v_hi - cal.v_lo;
        if (dv == 0.0f)
        {
            return cal.value_lo;
        }
        return cal.value_lo
               + ((v_source - cal.v_lo) * (cal.value_hi - cal.value_lo) / dv);
    }

    float ntc_resistance(const ThermistorCal &cal, float v_node)
    {
        if (cal.supply_volts <= 0.0f || cal.pullup_ohms <= 0.0f)
        {
            return -1.0f;
        }

        float denom = cal.supply_volts - v_node;
        if (denom <= (cal.supply_volts * OPEN_CIRCUIT_FRACTION))
        {
            /**
             * Node is sitting at the rail: the thermistor is not there.
             * Returning an enormous resistance instead would convert to a
             * merely very cold temperature, which is the exact failure
             * mode this module exists to catch. */
            return -1.0f;
        }
        if (v_node <= 0.0f)
        {
            return 0.0f;
        }

        return cal.pullup_ohms * v_node / denom;
    }

    float ntc_temp_beta(const ThermistorCal &cal, float ohms)
    {
        if (cal.beta == 0.0f || cal.r25_ohms <= 0.0f || ohms <= 0.0f)
        {
            return ABSURDLY_HOT_C;
        }

        float inv_t
            = (1.0f / KELVIN_25C) + (logf(ohms / cal.r25_ohms) / cal.beta);
        return temp_from_inverse_kelvin(inv_t);
    }

    float ntc_temp_steinhart(const ThermistorCal &cal, float ohms)
    {
        if (ohms <= 0.0f)
        {
            return ABSURDLY_HOT_C;
        }

        float ln_r = logf(ohms);
        float inv_t
            = cal.sh_a + (cal.sh_b * ln_r) + (cal.sh_c * ln_r * ln_r * ln_r);
        return temp_from_inverse_kelvin(inv_t);
    }

    float ntc_temp_lut(const ThermistorCal &cal, float ohms)
    {
        if (cal.lut_count == 0 || ohms <= 0.0f)
        {
            return ABSURDLY_HOT_C;
        }
        if (cal.lut_count == 1)
        {
            return cal.lut_temp_c[0];
        }

        /**
         * Table is ascending in temperature, so descending in resistance.
         * Find the segment `ohms` falls in, defaulting to the nearest end
         * segment so the extrapolation below is the same code as the
         * interpolation. */
        uint8_t last = static_cast<uint8_t>(cal.lut_count - 2);
        uint8_t seg  = last;
        for (uint8_t i = 0; i <= last; i++)
        {
            if (ohms >= cal.lut_ohms[i + 1])
            {
                seg = i;
                break;
            }
        }

        float r0 = cal.lut_ohms[seg];
        float r1 = cal.lut_ohms[seg + 1];
        float t0 = cal.lut_temp_c[seg];
        float t1 = cal.lut_temp_c[seg + 1];

        if (r0 <= 0.0f || r1 <= 0.0f)
        {
            return ABSURDLY_HOT_C;
        }

        float ln0 = logf(r0);
        float ln1 = logf(r1);
        if (ln0 == ln1)
        {
            return t0;
        }

        /**
         * Straight line in (ln R, 1/T) -- see the header for why this
         * space and not a chord between the raw points. */
        float inv0 = 1.0f / (t0 + KELVIN_0C);
        float inv1 = 1.0f / (t1 + KELVIN_0C);
        float f    = (logf(ohms) - ln0) / (ln1 - ln0);

        return temp_from_inverse_kelvin(inv0 + (f * (inv1 - inv0)));
    }

    float ntc_temp(const ThermistorCal &cal, float ohms)
    {
        switch (cal.model)
        {
            case CurveModel::Beta:
                return ntc_temp_beta(cal, ohms);
            case CurveModel::SteinhartHart:
                return ntc_temp_steinhart(cal, ohms);
            case CurveModel::Lut:
                return ntc_temp_lut(cal, ohms);
        }
        return ABSURDLY_HOT_C;
    }

    SensorSpan sensor_cal_span(const SensorCal &cal)
    {
        SensorSpan span { 0.0f, 0.0f, false };

        switch (cal.kind)
        {
            case SensorKind::LinearVoltage:
                ordered(cal.linear.value_lo,
                        cal.linear.value_hi,
                        span.min,
                        span.max);
                span.valid = span.max > span.min;
                break;

            case SensorKind::Thermistor:
                span.min   = cal.thermistor.valid_min_c;
                span.max   = cal.thermistor.valid_max_c;
                span.valid = span.max > span.min;
                break;

            case SensorKind::RailSense:
            case SensorKind::Unconfigured:
                break;
        }

        return span;
    }

    SensorReading sensor_convert(const SensorCal &cal,
                                 float            v_adc,
                                 float            rail_volts)
    {
        SensorReading out { 0.0f, SensorStatus::Unconfigured };

        if (cal.kind == SensorKind::Unconfigured)
        {
            return out;
        }

        float v_source = divider_source_volts(cal.divider, v_adc);

        if (cal.kind == SensorKind::LinearVoltage
            || cal.kind == SensorKind::RailSense)
        {
            /**
             * A rail cannot be measured relative to itself, so the
             * ratiometric flag is honoured only for a real sensor. */
            if (cal.kind == SensorKind::LinearVoltage && cal.linear.ratiometric
                && rail_volts > 0.0f && cal.linear.nominal_supply > 0.0f)
            {
                /**
                 * The fit's v_lo/v_hi were specified at the nominal
                 * supply, so referring the measurement back to that
                 * supply is what removes the sag. Note this also has to
                 * happen before the band check, or a sagging rail would
                 * read as a sensor drifting out of range. */
                v_source *= cal.linear.nominal_supply / rail_volts;
            }

            float lo = 0.0f;
            float hi = 0.0f;
            ordered(cal.linear.v_lo, cal.linear.v_hi, lo, hi);

            out.value  = linear_apply(cal.linear, v_source);
            out.status = band_check(v_source, lo, hi, cal.band_margin);
            return out;
        }

        /* Thermistor. */
        float ohms = ntc_resistance(cal.thermistor, v_source);
        if (ohms < 0.0f)
        {
            out.status = SensorStatus::OpenCircuit;
            return out;
        }
        if (ohms < MIN_PLAUSIBLE_OHMS)
        {
            out.status = SensorStatus::ShortCircuit;
            return out;
        }

        out.value  = ntc_temp(cal.thermistor, ohms);
        out.status = band_check(out.value,
                                cal.thermistor.valid_min_c,
                                cal.thermistor.valid_max_c,
                                cal.band_margin);
        return out;
    }

} // namespace core
