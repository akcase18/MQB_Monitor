#include "simulator_analog_input.hpp"

#include "simulator_clock.hpp"
#include "simulator_events.hpp"

#include <math.h>

namespace
{
    /* --- Front end, matching the build plan's channel table ------------ */

    /* 10k/15k divider: 5V in reads 3.0V at the ADC. */
    constexpr float DIVIDER_RATIO = 15.0f / (10.0f + 15.0f);

    /* NTC pull-up leg: R_up to 3.3V, thermistor to ground. */
    constexpr float NTC_PULLUP_OHMS = 2200.0f;
    constexpr float NTC_SUPPLY_V    = 3.3f;

    /**
     * Thermistor beta model. Representative automotive NTC values -- the
     * real part's curve comes from its datasheet and lives in config (see
     * the build plan), but conditioning has to be developed against
     * something, and something physically shaped beats a straight line. */
    constexpr float NTC_R25_OHMS = 2000.0f;
    constexpr float NTC_BETA     = 3500.0f;
    constexpr float KELVIN_0C    = 273.15f;

    /* Transducer: 0.5V at 0 psi, 4.5V at full scale, ratiometric on 5V. */
    constexpr float PRESSURE_V_MIN          = 0.5f;
    constexpr float PRESSURE_V_MAX          = 4.5f;
    constexpr float PRESSURE_FS_PSI         = 150.0f;
    constexpr float PRESSURE_NOMINAL_SUPPLY = 5.0f;

    /* --- The scripted drive ------------------------------------------- */

    /**
     * One full cold-start-to-hot cycle, then repeat. Long enough that the
     * warmup reads as gradual, short enough that a developer does not wait
     * around to see the hot end. */
    constexpr uint32_t CYCLE_MS = 180000;

    float clampf(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    /**
     * Exponential approach from `start` to `target`, `tau` in seconds --
     * the shape a thermal mass actually warms with. */
    float warm_to(float t_s, float start, float target, float tau_s)
    {
        return target + (start - target) * expf(-t_s / tau_s);
    }

    /** Coolant: warms first and fastest, then holds at thermostat temp. */
    float coolant_temp_c(float t_s)
    {
        return warm_to(t_s, 18.0f, 89.0f, 55.0f);
    }

    /** Oil: lags coolant substantially, and settles hotter. That ordering
     * is the physically right one and worth having in the fake data --
     * a dashboard showing oil hotter than coolant on a cold start would be
     * a bug worth noticing. */
    float oil_temp_c(float t_s)
    {
        return warm_to(t_s, 18.0f, 104.0f, 110.0f);
    }

    /**
     * Oil pressure: high while the oil is cold and thick, falling as it
     * thins, with a periodic drop to idle pressure.
     *
     * The idle dips are the interesting part -- they are what a low-pressure
     * warning threshold has to tolerate without crying wolf, and what makes
     * the RPM-less threshold problem visible in the simulator rather than
     * only in the car.
     */
    float oil_pressure_psi(float t_s)
    {
        float hot    = clampf((oil_temp_c(t_s) - 18.0f) / 86.0f, 0.0f, 1.0f);
        float cruise = 78.0f - (30.0f * hot); /* 78 psi cold -> 48 psi hot */
        float idle   = 34.0f - (18.0f * hot); /* 34 psi cold -> 16 psi hot */

        /**
         * 8s at idle out of every 25s, with ~1.5s of slope on each edge so
         * a filter under test has something with a real gradient to follow
         * rather than a step.
         *
         * Deliberately a pure function of phase -- an IIR smoothing term
         * here would make the result depend on how often this is called,
         * which would break the determinism the header promises and make a
         * failing test unreproducible. */
        constexpr float RAMP_S = 1.5f;
        float           phase  = fmodf(t_s, 25.0f);
        float           rise   = clampf((phase - 12.0f) / RAMP_S, 0.0f, 1.0f);
        float           fall   = clampf((20.0f - phase) / RAMP_S, 0.0f, 1.0f);
        float           blend  = rise * fall;
        blend = blend * blend * (3.0f - (2.0f * blend)); /* smoothstep */

        return cruise + ((idle - cruise) * blend);
    }

    /** Supply rail, sagging slightly under load. Gives core/sensors'
     * ratiometric correction something real to correct for. */
    float rail_volts(float t_s)
    {
        return 5.02f - (0.14f * (0.5f + (0.5f * sinf(t_s * 0.21f))));
    }

    /* --- Inverse transfer functions: engineering units -> ADC volts ---- */

    float pressure_to_adc_v(float psi, float supply_v)
    {
        float frac = clampf(psi / PRESSURE_FS_PSI, 0.0f, 1.0f);
        /**
         * Ratiometric: the transducer's output scales with its supply, so
         * a sagging rail moves the reading unless corrected. */
        float ratio = supply_v / PRESSURE_NOMINAL_SUPPLY;
        float sensor_v
            = (PRESSURE_V_MIN + ((PRESSURE_V_MAX - PRESSURE_V_MIN) * frac))
              * ratio;
        return sensor_v * DIVIDER_RATIO;
    }

    float ntc_resistance(float temp_c)
    {
        float t_k = temp_c + KELVIN_0C;
        return NTC_R25_OHMS
               * expf(NTC_BETA * ((1.0f / t_k) - (1.0f / (25.0f + KELVIN_0C))));
    }

    float temp_to_adc_v(float temp_c)
    {
        float r = ntc_resistance(temp_c);
        return NTC_SUPPLY_V * (r / (r + NTC_PULLUP_OHMS));
    }

    float rail_to_adc_v(float supply_v)
    {
        return supply_v * DIVIDER_RATIO;
    }

    /**
     * What the transducer reads while the pressure-drop key is held.
     * Well below hot-idle pressure -- the point is a number that is
     * unambiguously a *fault* rather than a stoplight, so a threshold set
     * conservatively under idle (see the build plan's 9.1) still catches
     * it. Still comfortably inside the sensor's valid band, so this
     * produces a believable low reading rather than the no-reading-at-all
     * an open circuit gives. */
    constexpr float DROPPED_PRESSURE_PSI = 5.0f;

    /* Milliseconds since boot -> seconds into the repeating cycle. */
    float cycle_seconds(uint32_t t_ms)
    {
        return static_cast<float>(t_ms % CYCLE_MS) / 1000.0f;
    }
} // namespace

SimulatorAnalogInput &SimulatorAnalogInput::instance()
{
    static SimulatorAnalogInput s_instance;
    return s_instance;
}

void SimulatorAnalogInput::begin()
{
}

uint8_t SimulatorAnalogInput::channel_count() const
{
    return CHANNEL_COUNT;
}

void SimulatorAnalogInput::service_fault_key()
{
    if (!simulator_events::consume_fault_key())
    {
        return;
    }

    /* -1 -> 0 -> 1 -> 2 -> 3 -> -1. */
    fault_channel_++;
    if (fault_channel_ >= static_cast<int8_t>(CHANNEL_COUNT))
    {
        fault_channel_ = -1;
    }
}

float SimulatorAnalogInput::scripted_oil_pressure_psi(uint32_t t_ms)
{
    return oil_pressure_psi(cycle_seconds(t_ms));
}

float SimulatorAnalogInput::scripted_oil_temp_c(uint32_t t_ms)
{
    return oil_temp_c(cycle_seconds(t_ms));
}

float SimulatorAnalogInput::scripted_coolant_temp_c(uint32_t t_ms)
{
    return coolant_temp_c(cycle_seconds(t_ms));
}

float SimulatorAnalogInput::scripted_rail_volts(uint32_t t_ms)
{
    return rail_volts(cycle_seconds(t_ms));
}

bool SimulatorAnalogInput::read_microvolts(uint8_t ch, uint32_t &out)
{
    /**
     * Pumping here as well as in the touch driver is what keeps the window
     * alive on a product with no touchscreen -- see simulator_events.hpp. */
    simulator_events::pump();
    service_fault_key();

    uint32_t t_ms = SimulatorClock::instance().millis();

    /**
     * Applied here rather than inside microvolts_at() so that function
     * stays a pure function of its arguments -- a host test asserting the
     * conditioning round trip must not depend on which keys happen to be
     * held while it runs. The fault channel is passed in for the same
     * reason; this one is a live key with no natural argument, since
     * nothing about it is worth replaying. */
    if (ch == CH_OIL_PRESSURE
        && static_cast<int8_t>(ch) != fault_channel_
        && simulator_events::pressure_drop_key_down())
    {
        float t_s = cycle_seconds(t_ms);
        float volts
            = pressure_to_adc_v(DROPPED_PRESSURE_PSI, rail_volts(t_s));
        out = static_cast<uint32_t>((volts * 1000000.0f) + 0.5f);
        return true;
    }

    return microvolts_at(ch, t_ms, fault_channel_, out);
}

bool SimulatorAnalogInput::microvolts_at(uint8_t   ch,
                                         uint32_t  t_ms,
                                         int8_t    fault_channel,
                                         uint32_t &out)
{
    if (ch >= CHANNEL_COUNT)
    {
        return false;
    }

    float t_s = cycle_seconds(t_ms);

    float volts = 0.0f;

    if (static_cast<int8_t>(ch) == fault_channel)
    {
        /**
         * An open circuit, not a read failure. The wire is disconnected but
         * the ADC is perfectly healthy and reports a real voltage -- for an
         * NTC leg that is the pull-up rail, for a transducer input it is
         * whatever the bias network holds it at. This is exactly the case
         * that makes analog dangerous: nothing errors, and the number is
         * plausible unless something checks it against a valid band.
         *
         * read_microvolts() therefore returns TRUE here. Returning false
         * would be modelling a broken ADC, which is a different fault and
         * a much easier one to catch. */
        volts = (ch == CH_OIL_TEMP || ch == CH_COOLANT_TEMP) ? NTC_SUPPLY_V
                                                             : 0.0f;
    }
    else
    {
        switch (ch)
        {
            case CH_OIL_PRESSURE:
                volts
                    = pressure_to_adc_v(oil_pressure_psi(t_s), rail_volts(t_s));
                break;
            case CH_OIL_TEMP:
                volts = temp_to_adc_v(oil_temp_c(t_s));
                break;
            case CH_COOLANT_TEMP:
                volts = temp_to_adc_v(coolant_temp_c(t_s));
                break;
            case CH_RAIL_SENSE:
                volts = rail_to_adc_v(rail_volts(t_s));
                break;
            default:
                return false;
        }
    }

    if (volts < 0.0f)
    {
        volts = 0.0f;
    }

    out = static_cast<uint32_t>((volts * 1000000.0f) + 0.5f);
    return true;
}
