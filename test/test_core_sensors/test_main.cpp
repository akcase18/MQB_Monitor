/**
 * @file test_main.cpp
 * @brief core/sensors: the arithmetic between an ADC pin and a gauge.
 *
 * Two kinds of test live here and they are checking different things.
 *
 * The first kind is ordinary unit testing of pure functions -- divider
 * maths, the two-point fit across several output spans, all three curve
 * models, filter step response.
 *
 * The second kind is the one this phase exists for. The simulator's sensor
 * model is written *backwards*: its waveforms are scripted in psi and
 * degrees C and then pushed through the inverse of each sensor's transfer
 * function to produce ADC volts. So running core/sensors forwards over
 * those volts has to land back on the number the simulator started from.
 * A simulator emitting convenient voltages directly would let a wrong
 * transfer function pass; this closes that loop, in both directions, with
 * the plausibility check in the middle.
 *
 * Run with `pio test -e simulator`.
 */
#include <math.h>
#include <unity.h>

#include "analog_signal_source.hpp"
#include "conditioning.hpp"
#include "curve_library.hpp"
#include "sensor_filter.hpp"
#include "signal_catalog.hpp"
#include "signal_store.hpp"
#include "simulator_analog_input.hpp"

namespace
{
    /**
     * The simulator's front end, restated here as a calibration. These
     * must match the constants in simulator_analog_input.cpp -- that is
     * the whole point of the round-trip tests below, which fail loudly if
     * either side drifts. */
    constexpr float DIV_R_TOP    = 10000.0f;
    constexpr float DIV_R_BOTTOM = 15000.0f;

    constexpr float NTC_PULLUP = 2200.0f;
    constexpr float NTC_SUPPLY = 3.3f;
    constexpr float NTC_R25    = 2000.0f;
    constexpr float NTC_BETA   = 3500.0f;

    constexpr float PRESSURE_FS_PSI = 150.0f;
    constexpr float PSI_PER_BAR     = 14.5038f;

    /* Simulator channel assignment. */
    constexpr uint8_t CH_PRESSURE = 0;
    constexpr uint8_t CH_OIL_T    = 1;
    constexpr uint8_t CH_COOLANT  = 2;
    constexpr uint8_t CH_RAIL     = 3;

    core::SensorCal pressure_cal(uint16_t signal_id)
    {
        core::SensorCal cal {};
        cal.kind      = core::SensorKind::LinearVoltage;
        cal.channel   = CH_PRESSURE;
        cal.signal_id = signal_id;

        cal.divider.r_top    = DIV_R_TOP;
        cal.divider.r_bottom = DIV_R_BOTTOM;

        cal.linear.v_lo           = 0.5f;
        cal.linear.value_lo       = 0.0f;
        cal.linear.v_hi           = 4.5f;
        cal.linear.value_hi       = PRESSURE_FS_PSI / PSI_PER_BAR;
        cal.linear.ratiometric    = true;
        cal.linear.nominal_supply = 5.0f;

        cal.filter_tau_s = 0.0f; /**
                                  * unfiltered, so a single sample is the
                                  * conditioned value exactly */
        cal.band_margin = core::SENSOR_DEFAULT_BAND_MARGIN;
        return cal;
    }

    core::SensorCal ntc_cal(uint8_t channel, uint16_t signal_id)
    {
        core::SensorCal cal {};
        cal.kind      = core::SensorKind::Thermistor;
        cal.channel   = channel;
        cal.signal_id = signal_id;

        cal.thermistor.pullup_ohms  = NTC_PULLUP;
        cal.thermistor.supply_volts = NTC_SUPPLY;
        cal.thermistor.model        = core::CurveModel::Beta;
        cal.thermistor.r25_ohms     = NTC_R25;
        cal.thermistor.beta         = NTC_BETA;
        cal.thermistor.valid_min_c  = -40.0f;
        cal.thermistor.valid_max_c  = 150.0f;

        cal.filter_tau_s = 0.0f;
        cal.band_margin  = core::SENSOR_DEFAULT_BAND_MARGIN;
        return cal;
    }

    core::SensorCal rail_cal()
    {
        core::SensorCal cal {};
        core::sensor_preset_apply(20, cal);
        cal.channel      = CH_RAIL;
        cal.signal_id    = core::SENSOR_SIGNAL_NONE;
        cal.filter_tau_s = 0.0f;
        return cal;
    }

    /**
     * An IAnalogInput that replays the simulator's model at an instant the
     * test chooses, so nothing here depends on when it ran. */
    class ScriptedAdc : public IAnalogInput
    {
        public:
            void begin() override
            {
            }

            uint8_t channel_count() const override
            {
                return SimulatorAnalogInput::CHANNEL_COUNT;
            }

            bool read_microvolts(uint8_t ch, uint32_t &out) override
            {
                reads++;
                return SimulatorAnalogInput::microvolts_at(
                    ch, t_ms, fault_channel, out);
            }

            uint32_t t_ms          = 0;
            int8_t   fault_channel = -1;
            uint32_t reads         = 0;
    };

    /* Volts the simulator reports on a channel at a given instant. */
    float adc_volts(uint8_t ch, uint32_t t_ms, int8_t fault = -1)
    {
        uint32_t uv = 0;
        TEST_ASSERT_TRUE(
            SimulatorAnalogInput::microvolts_at(ch, t_ms, fault, uv));
        return static_cast<float>(uv) / 1000000.0f;
    }
} // namespace

/**
 * ==============================================================================
 * Pure conditioning maths
 =============================================================================*/

void test_divider_is_undone_exactly(void)
{
    core::Divider div { DIV_R_TOP, DIV_R_BOTTOM };

    /* 10k/15k is a 0.6x divider: 5.0V in reads 3.0V at the pin. */
    TEST_ASSERT_FLOAT_WITHIN(
        0.0005f, 5.0f, core::divider_source_volts(div, 3.0f));

    /**
     * An unconfigured divider must be a pass-through rather than a
     * division by zero -- a thermistor channel has no divider at all. */
    core::Divider none { 0.0f, 0.0f };
    TEST_ASSERT_FLOAT_WITHIN(
        0.0f, 1.234f, core::divider_source_volts(none, 1.234f));
}

void test_two_point_fit_is_not_hardcoded_to_any_output_span(void)
{
    /**
     * The same physical pressure, read through three different senders.
     * If any part of a 0.5-4.5V part were baked in, two of these fail. */
    struct Case
    {
            float v_lo;
            float v_hi;
    };
    const Case cases[] = { { 0.5f, 4.5f }, { 0.0f, 5.0f }, { 1.0f, 5.0f } };

    for (const Case &c : cases)
    {
        core::LinearCal cal {};
        cal.v_lo     = c.v_lo;
        cal.value_lo = 0.0f;
        cal.v_hi     = c.v_hi;
        cal.value_hi = 10.0f;

        /* Both endpoints, and the midpoint, in that sender's own terms. */
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, core::linear_apply(cal, c.v_lo));
        TEST_ASSERT_FLOAT_WITHIN(
            0.001f, 10.0f, core::linear_apply(cal, c.v_hi));
        TEST_ASSERT_FLOAT_WITHIN(
            0.001f, 5.0f, core::linear_apply(cal, (c.v_lo + c.v_hi) * 0.5f));
    }
}

void test_two_point_fit_handles_a_descending_sender(void)
{
    /**
     * Some senders fall with the measurand. Nothing in the fit assumes
     * v_hi > v_lo, and the band check orders the pair itself. */
    core::SensorCal cal {};
    cal.kind            = core::SensorKind::LinearVoltage;
    cal.linear.v_lo     = 4.5f;
    cal.linear.value_lo = 0.0f;
    cal.linear.v_hi     = 0.5f;
    cal.linear.value_hi = 100.0f;
    cal.band_margin     = 0.05f;

    core::SensorReading r = core::sensor_convert(cal, 2.5f, 0.0f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::SensorStatus::Ok),
                          static_cast<int>(r.status));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, r.value);
}

void test_beta_model_hits_its_own_reference_point(void)
{
    core::ThermistorCal cal {};
    cal.model    = core::CurveModel::Beta;
    cal.r25_ohms = 10000.0f;
    cal.beta     = 3435.0f;

    /* By definition R25 is the resistance at 25 C. */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, core::ntc_temp_beta(cal, 10000.0f));

    /**
     * And the model's own inverse, at a temperature away from the
     * reference, must come back. */
    const float t_k = 85.0f + 273.15f;
    const float ohms
        = 10000.0f * expf(3435.0f * ((1.0f / t_k) - (1.0f / 298.15f)));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 85.0f, core::ntc_temp_beta(cal, ohms));

    /**
     * Monotonic and the right way round: an NTC gets *less* resistive as
     * it heats. A sign slip here would read a cold engine as overheating. */
    TEST_ASSERT_TRUE(core::ntc_temp_beta(cal, 1000.0f)
                     > core::ntc_temp_beta(cal, 100000.0f));
}

void test_steinhart_hart_matches_three_known_points(void)
{
    /**
     * Coefficients fitted to a 10k B=3435 part at -10/25/85 C, so the
     * comparison below is against an independent model rather than
     * against this function's own output. */
    core::ThermistorCal beta {};
    beta.model    = core::CurveModel::Beta;
    beta.r25_ohms = 10000.0f;
    beta.beta     = 3435.0f;

    const float temps[3] = { -10.0f, 25.0f, 85.0f };
    float       ln_r[3];
    float       inv_t[3];
    for (int i = 0; i < 3; i++)
    {
        float t_k = temps[i] + 273.15f;
        ln_r[i]   = logf(10000.0f
                       * expf(3435.0f * ((1.0f / t_k) - (1.0f / 298.15f))));
        inv_t[i]  = 1.0f / t_k;
    }

    /* Solve the 3x3 for A, B, C by elimination. */
    float l1 = ln_r[0], l2 = ln_r[1], l3 = ln_r[2];
    float y1 = inv_t[0], y2 = inv_t[1], y3 = inv_t[2];

    float d21 = l2 - l1;
    float d31 = l3 - l1;
    float c21 = (l2 * l2 * l2) - (l1 * l1 * l1);
    float c31 = (l3 * l3 * l3) - (l1 * l1 * l1);

    float c
        = (((y3 - y1) * d21) - ((y2 - y1) * d31)) / ((c31 * d21) - (c21 * d31));
    float b = ((y2 - y1) - (c * c21)) / d21;
    float a = y1 - (b * l1) - (c * l1 * l1 * l1);

    core::ThermistorCal sh {};
    sh.model = core::CurveModel::SteinhartHart;
    sh.sh_a  = a;
    sh.sh_b  = b;
    sh.sh_c  = c;

    for (int i = 0; i < 3; i++)
    {
        TEST_ASSERT_FLOAT_WITHIN(
            0.2f, temps[i], core::ntc_temp_steinhart(sh, expf(ln_r[i])));
    }
}

void test_lut_interpolates_between_points_and_extrapolates_past_them(void)
{
    /**
     * A table sampled from a Beta curve. Because the LUT interpolates in
     * (ln R, 1/T) -- the space an NTC is actually linear in -- it must
     * reproduce that curve *between* its points, not chord across it.
     * Sampling every 20 C leaves a lot of room for a naive linear-in-R
     * interpolation to be badly wrong at the midpoints. */
    core::ThermistorCal beta {};
    beta.model    = core::CurveModel::Beta;
    beta.r25_ohms = 10000.0f;
    beta.beta     = 3435.0f;

    auto r_at = [&](float temp_c) {
        float t_k = temp_c + 273.15f;
        return 10000.0f * expf(3435.0f * ((1.0f / t_k) - (1.0f / 298.15f)));
    };

    core::ThermistorCal lut {};
    lut.model     = core::CurveModel::Lut;
    lut.lut_count = 10;
    for (uint8_t i = 0; i < lut.lut_count; i++)
    {
        float t           = -40.0f + (20.0f * static_cast<float>(i));
        lut.lut_temp_c[i] = t;
        lut.lut_ohms[i]   = r_at(t);
    }

    /* At the points. */
    TEST_ASSERT_FLOAT_WITHIN(
        0.05f, -40.0f, core::ntc_temp_lut(lut, r_at(-40.0f)));
    TEST_ASSERT_FLOAT_WITHIN(
        0.05f, 140.0f, core::ntc_temp_lut(lut, r_at(140.0f)));

    /* Between them. */
    TEST_ASSERT_FLOAT_WITHIN(
        0.05f, 30.0f, core::ntc_temp_lut(lut, r_at(30.0f)));
    TEST_ASSERT_FLOAT_WITHIN(
        0.05f, 95.0f, core::ntc_temp_lut(lut, r_at(95.0f)));

    /**
     * Beyond both ends it must keep going, not clamp. Clamping would
     * manufacture a valid-looking reading from a sensor that has left its
     * calibrated range -- the band check can only reject what it is shown. */
    TEST_ASSERT_TRUE(core::ntc_temp_lut(lut, r_at(-60.0f)) < -40.0f);
    TEST_ASSERT_TRUE(core::ntc_temp_lut(lut, r_at(170.0f)) > 140.0f);
}

void test_ratiometric_correction_holds_a_reading_through_a_sagging_rail(void)
{
    core::SensorCal cal = pressure_cal(core::SIGNAL_CATALOG[0].id);

    /**
     * The same 50% pressure, seen through a healthy rail and a sagging
     * one. A ratiometric sender's output moves with its supply, so the
     * raw volts differ -- the corrected reading must not. */
    const float frac      = 0.5f;
    const float sensor_at = [&](float supply) {
        return (0.5f + (4.0f * frac)) * (supply / 5.0f);
    }(5.00f);
    const float sensor_sag = (0.5f + (4.0f * frac)) * (4.60f / 5.0f);

    const float ratio = DIV_R_BOTTOM / (DIV_R_TOP + DIV_R_BOTTOM);

    core::SensorReading good
        = core::sensor_convert(cal, sensor_at * ratio, 5.00f);
    core::SensorReading sag
        = core::sensor_convert(cal, sensor_sag * ratio, 4.60f);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::SensorStatus::Ok),
                          static_cast<int>(good.status));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::SensorStatus::Ok),
                          static_cast<int>(sag.status));
    TEST_ASSERT_FLOAT_WITHIN(0.005f, good.value, sag.value);

    /**
     * And without the correction the same sag would have moved it -- so
     * the assertion above is testing something. */
    core::SensorReading uncorrected
        = core::sensor_convert(cal, sensor_sag * ratio, 0.0f);
    TEST_ASSERT_TRUE(fabsf(uncorrected.value - good.value) > 0.1f);
}

/**
 * ==============================================================================
 * Plausibility -- the part that matters
 =============================================================================*/

void test_an_open_thermistor_leg_is_rejected_not_believed(void)
{
    core::SensorCal cal = ntc_cal(CH_OIL_T, core::SIGNAL_CATALOG[0].id);

    /**
     * An open NTC leg leaves the node at the pull-up rail. The arithmetic
     * does not fail -- it diverges quietly, and a naive implementation
     * reports a very cold but entirely believable temperature. */
    core::SensorReading r = core::sensor_convert(cal, NTC_SUPPLY, 0.0f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::SensorStatus::OpenCircuit),
                          static_cast<int>(r.status));
    TEST_ASSERT_FALSE(core::sensor_reading_is_usable(r.status));

    /* A shorted leg is the other end of the same divider. */
    core::SensorReading s = core::sensor_convert(cal, 0.0f, 0.0f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::SensorStatus::ShortCircuit),
                          static_cast<int>(s.status));
}

void test_the_band_follows_the_calibration_rather_than_a_constant(void)
{
    /**
     * A sender wired to a channel calibrated for a *different* sender
     * reads a plausible number. The only thing that catches it is the
     * band being derived from the calibration, so that re-calibrating
     * moves the band with it. */
    core::SensorCal narrow {};
    narrow.kind            = core::SensorKind::LinearVoltage;
    narrow.linear.v_lo     = 0.5f;
    narrow.linear.value_lo = 0.0f;
    narrow.linear.v_hi     = 4.5f;
    narrow.linear.value_hi = 10.0f;
    narrow.band_margin     = 0.05f;

    /* 0.1 V is below 0.5 by more than 5% of the 4 V span. */
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::SensorStatus::BelowRange),
        static_cast<int>(core::sensor_convert(narrow, 0.1f, 0.0f).status));

    /**
     * Re-calibrate the same channel for a 0-5 V sender and that identical
     * voltage becomes a legitimate low reading. No recompile. */
    core::SensorCal wide = narrow;
    wide.linear.v_lo     = 0.0f;
    wide.linear.v_hi     = 5.0f;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(core::SensorStatus::Ok),
        static_cast<int>(core::sensor_convert(wide, 0.1f, 0.0f).status));

    /**
     * And the span both the display range and the band derive from moved
     * with it -- that shared derivation is what keeps them in step. */
    core::SensorSpan span = core::sensor_cal_span(wide);
    TEST_ASSERT_TRUE(span.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, span.min);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, span.max);
}

void test_an_out_of_band_reading_produces_no_write(void)
{
    ScriptedAdc       adc;
    core::SignalStore store;

    const uint16_t press_id = core::SIGNAL_CATALOG[0].id;
    const uint16_t oil_id   = core::SIGNAL_CATALOG[1].id;

    core::SensorCal cals[3]
        = { pressure_cal(press_id), ntc_cal(CH_OIL_T, oil_id), rail_cal() };

    core::AnalogSignalSource source(adc, store);
    source.configure(cals, 3);
    source.set_sample_interval_ms(1);

    const uint16_t ids[2] = { press_id, oil_id };
    source.set_active(ids, 2);

    /* Healthy first, so there is a value to lose. */
    adc.t_ms = 60000;
    for (int i = 0; i < 40; i++)
    {
        source.tick(60000 + static_cast<uint32_t>(i));
    }
    TEST_ASSERT_TRUE(store.get(oil_id).has_data);

    float    last_value  = store.get(oil_id).value;
    uint32_t last_update = store.get(oil_id).last_update_ms;

    /**
     * Now pull the oil temperature sender's wire. The ADC keeps working
     * and keeps returning true -- it reports the pull-up rail, which is a
     * real voltage. Nothing errors. */
    adc.fault_channel = static_cast<int8_t>(CH_OIL_T);
    for (int i = 0; i < 40; i++)
    {
        source.tick(70000 + static_cast<uint32_t>(i));
    }

    /**
     * The store must be untouched: no zero, no -86 C, not even a refreshed
     * timestamp on the old value. A moving timestamp would make a dead
     * sensor look alive to core::Logger's on-change spacing. */
    TEST_ASSERT_EQUAL_UINT32(last_update, store.get(oil_id).last_update_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, last_value, store.get(oil_id).value);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::SensorStatus::OpenCircuit),
                          static_cast<int>(source.channel_state(1).status));

    /**
     * The other channel is unaffected -- one dead sensor must not blank
     * the dashboard. */
    TEST_ASSERT_TRUE(store.get(press_id).has_data);

    /**
     * Now the same thing on the *linear* channel, which fails by a
     * different route and must be stopped by a different guard: a
     * disconnected transducer input drifts to 0 V, which is not an open
     * circuit the divider maths can spot -- only the band derived from the
     * sender's own 0.5 V offset rejects it. Without this case, deleting
     * the band check entirely still leaves the thermistor covered and the
     * suite green. */
    float    press_value  = store.get(press_id).value;
    uint32_t press_update = store.get(press_id).last_update_ms;

    adc.fault_channel = static_cast<int8_t>(CH_PRESSURE);
    for (int i = 0; i < 40; i++)
    {
        source.tick(80000 + static_cast<uint32_t>(i));
    }

    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::SensorStatus::BelowRange),
                          static_cast<int>(source.channel_state(0).status));
    TEST_ASSERT_EQUAL_UINT32(press_update, store.get(press_id).last_update_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, press_value, store.get(press_id).value);
}

/**
 * ==============================================================================
 * Filtering
 =============================================================================*/

void test_ema_step_response_matches_its_time_constant(void)
{
    core::SensorFilter filter;
    filter.configure(1.0f); /* tau = 1 s */

    TEST_ASSERT_FALSE(filter.has_value());
    filter.update(0.0f, 0);
    TEST_ASSERT_TRUE(filter.has_value());

    /* Settle at zero so the median has no history left from priming. */
    for (int i = 0; i < 20; i++)
    {
        filter.update(0.0f, 50);
    }

    /**
     * Step to 1.0 and hold for exactly one time constant. A first-order
     * lag is at 1 - 1/e = 63.2% by then, whatever step size is used to
     * get there -- which is what makes this an assertion about tau rather
     * than about the update cadence. */
    float v = 0.0f;
    for (int i = 0; i < 20; i++)
    {
        v = filter.update(1.0f, 50);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.632f, v);

    /* Four time constants in and it is essentially there. */
    for (int i = 0; i < 60; i++)
    {
        v = filter.update(1.0f, 50);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.0f, v);
}

void test_time_constant_is_independent_of_sample_cadence(void)
{
    /**
     * The same tau, sampled at 20 Hz and at 80 Hz, must reach the same
     * place after the same elapsed *time*. Without a dt-aware alpha,
     * configuring a fourth channel would silently slow the other three. */
    core::SensorFilter slow;
    core::SensorFilter fast;
    slow.configure(1.0f);
    fast.configure(1.0f);

    slow.update(0.0f, 0);
    fast.update(0.0f, 0);

    float s = 0.0f;
    float f = 0.0f;
    for (int i = 0; i < 20; i++) /* 20 x 50ms = 1s */
    {
        s = slow.update(1.0f, 50);
    }
    for (int i = 0; i < 80; i++) /* 80 x 12.5ms ~ 1s */
    {
        f = fast.update(1.0f, 12);
    }

    TEST_ASSERT_FLOAT_WITHIN(0.02f, s, f);
}

void test_median_rejects_a_single_sample_spike(void)
{
    core::SensorFilter filter;
    filter.configure(0.0f); /* median only, so the effect is unmixed */

    filter.update(10.0f, 0);
    filter.update(10.0f, 10);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, filter.update(10.0f, 10));

    /* One wild sample between good ones must not reach the output at all. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, filter.update(9000.0f, 10));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, filter.update(10.0f, 10));
}

/**
 * ==============================================================================
 * The preset library
 =============================================================================*/

void test_a_preset_fills_the_calibration_and_is_then_irrelevant(void)
{
    core::SensorCal cal {};
    cal.channel   = 2;
    cal.signal_id = 7;

    TEST_ASSERT_TRUE(core::sensor_preset_apply(10, cal));

    /* Wiring survives -- picking a different sender does not move a wire. */
    TEST_ASSERT_EQUAL_UINT8(2, cal.channel);
    TEST_ASSERT_EQUAL_UINT16(7, cal.signal_id);

    /* The part description does not. */
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::SensorKind::Thermistor),
                          static_cast<int>(cal.kind));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 10000.0f, cal.thermistor.r25_ohms);
    TEST_ASSERT_EQUAL_UINT16(10, cal.preset_id);

    /**
     * Editing a field after applying a preset is an ordinary thing to do,
     * not a mode switch: the numbers are now the calibration's own, and
     * nothing re-reads the preset. This is what lets "pick from a list"
     * and "enter your own curve" be one form rather than two. */
    cal.thermistor.beta = 3950.0f;
    core::SensorReading r
        = core::sensor_convert(cal, 1.65f, 0.0f); /* half the 3.3V rail */
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::SensorStatus::Ok),
                          static_cast<int>(r.status));

    /**
     * An unknown id must degrade to "leave it alone", never to a wrong
     * curve -- a saved config outliving the firmware that wrote it is the
     * normal case, not an edge one. */
    core::SensorCal before = cal;
    TEST_ASSERT_FALSE(core::sensor_preset_apply(60000, cal));
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f, before.thermistor.beta, cal.thermistor.beta);
}

void test_every_preset_is_self_consistent(void)
{
    uint8_t count = core::sensor_preset_count();
    TEST_ASSERT_TRUE(count > 0);

    for (uint8_t i = 0; i < count; i++)
    {
        const core::SensorPreset &preset = core::sensor_preset_at(i);

        TEST_ASSERT_NOT_NULL(preset.name);
        TEST_ASSERT_NOT_NULL(preset.note);
        TEST_ASSERT_NOT_EQUAL(core::SENSOR_PRESET_CUSTOM, preset.id);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(preset.kind),
                              static_cast<int>(preset.cal.kind));
        TEST_ASSERT_EQUAL_UINT16(preset.id, preset.cal.preset_id);

        /**
         * Ids must be unique, or sensor_preset_find() silently returns
         * whichever came first. */
        TEST_ASSERT_EQUAL_PTR(&preset, core::sensor_preset_find(preset.id));

        /**
         * Every preset except the rail sense has to describe a span, since
         * that span is what seeds both the display range and the fault
         * window. */
        core::SensorSpan span = core::sensor_cal_span(preset.cal);
        if (preset.kind != core::SensorKind::RailSense)
        {
            TEST_ASSERT_TRUE(span.valid);
        }
    }
}

/**
 * ==============================================================================
 * The round trip -- engineering units out must equal engineering units in
 =============================================================================*/

void test_conditioning_recovers_the_simulators_scripted_values(void)
{
    core::SensorCal press = pressure_cal(0);
    core::SensorCal oil   = ntc_cal(CH_OIL_T, 0);
    core::SensorCal cool  = ntc_cal(CH_COOLANT, 0);
    core::SensorCal rail  = rail_cal();

    /* Across the whole warmup cycle, including the idle pressure dips. */
    for (uint32_t t = 0; t < 180000; t += 1373)
    {
        /**
         * The rail first: the pressure channel is ratiometric, so it is
         * only recoverable if the rail is. */
        core::SensorReading rail_r
            = core::sensor_convert(rail, adc_volts(CH_RAIL, t), 0.0f);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(core::SensorStatus::Ok),
                              static_cast<int>(rail_r.status));
        TEST_ASSERT_FLOAT_WITHIN(
            0.01f, SimulatorAnalogInput::scripted_rail_volts(t), rail_r.value);

        core::SensorReading p = core::sensor_convert(
            press, adc_volts(CH_PRESSURE, t), rail_r.value);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(core::SensorStatus::Ok),
                              static_cast<int>(p.status));

        /**
         * The simulator scripts psi; the store holds metric. Comparing in
         * psi keeps the assertion in the units the model was written in. */
        float psi = p.value * PSI_PER_BAR;
        TEST_ASSERT_FLOAT_WITHIN(
            0.15f, SimulatorAnalogInput::scripted_oil_pressure_psi(t), psi);

        core::SensorReading ot
            = core::sensor_convert(oil, adc_volts(CH_OIL_T, t), 0.0f);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(core::SensorStatus::Ok),
                              static_cast<int>(ot.status));
        TEST_ASSERT_FLOAT_WITHIN(
            0.15f, SimulatorAnalogInput::scripted_oil_temp_c(t), ot.value);

        core::SensorReading ct
            = core::sensor_convert(cool, adc_volts(CH_COOLANT, t), 0.0f);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(core::SensorStatus::Ok),
                              static_cast<int>(ct.status));
        TEST_ASSERT_FLOAT_WITHIN(
            0.15f, SimulatorAnalogInput::scripted_coolant_temp_c(t), ct.value);
    }
}

void test_source_writes_conditioned_values_into_the_store(void)
{
    ScriptedAdc       adc;
    core::SignalStore store;

    const uint16_t press_id = core::SIGNAL_CATALOG[0].id;
    const uint16_t oil_id   = core::SIGNAL_CATALOG[1].id;

    core::SensorCal cals[3]
        = { pressure_cal(press_id), ntc_cal(CH_OIL_T, oil_id), rail_cal() };

    core::AnalogSignalSource source(adc, store);
    source.configure(cals, 3);
    source.set_sample_interval_ms(1);

    const uint16_t ids[2] = { press_id, oil_id };
    source.set_active(ids, 2);

    /* 90 s in: warm, and away from a pressure dip edge. */
    adc.t_ms = 90000;
    for (int i = 0; i < 60; i++)
    {
        source.tick(90000 + static_cast<uint32_t>(i));
    }

    TEST_ASSERT_TRUE(store.get(press_id).has_data);
    TEST_ASSERT_TRUE(store.get(oil_id).has_data);

    TEST_ASSERT_FLOAT_WITHIN(0.2f,
                             SimulatorAnalogInput::scripted_oil_temp_c(90000),
                             store.get(oil_id).value);
    TEST_ASSERT_FLOAT_WITHIN(
        0.2f,
        SimulatorAnalogInput::scripted_oil_pressure_psi(90000),
        store.get(press_id).value * PSI_PER_BAR);

    /**
     * Driven through the interface, exactly as the composition root does,
     * to prove the analog source is substitutable for the UDS one. */
    core::ISignalSource &iface = source;
    iface.tick(90100);
    TEST_ASSERT_TRUE(store.get(press_id).has_data);
}

void test_only_displayed_signals_cost_a_conversion(void)
{
    ScriptedAdc       adc;
    core::SignalStore store;

    const uint16_t press_id = core::SIGNAL_CATALOG[0].id;
    const uint16_t oil_id   = core::SIGNAL_CATALOG[1].id;

    core::SensorCal cals[3]
        = { pressure_cal(press_id), ntc_cal(CH_OIL_T, oil_id), rail_cal() };

    core::AnalogSignalSource source(adc, store);
    source.configure(cals, 3);
    source.set_sample_interval_ms(1);

    /**
     * Only the temperature is on a panel. The pressure channel must not be
     * read at all -- and the rail must not be either, since nothing
     * ratiometric is being displayed to correct. */
    const uint16_t ids[1] = { oil_id };
    source.set_active(ids, 1);

    adc.t_ms = 90000;
    for (int i = 0; i < 30; i++)
    {
        source.tick(90000 + static_cast<uint32_t>(i));
    }

    TEST_ASSERT_TRUE(store.get(oil_id).has_data);
    TEST_ASSERT_FALSE(store.get(press_id).has_data);
    TEST_ASSERT_EQUAL_UINT32(0, source.channel_state(0).last_sample_ms);
    TEST_ASSERT_EQUAL_UINT32(0, source.channel_state(2).last_sample_ms);

    /**
     * Put the ratiometric channel on a panel and the rail comes back with
     * it, unasked -- three good sensors would otherwise read wrong
     * together when the supply sagged. */
    const uint16_t both[2] = { oil_id, press_id };
    source.set_active(both, 2);
    for (int i = 0; i < 30; i++)
    {
        source.tick(91000 + static_cast<uint32_t>(i));
    }
    TEST_ASSERT_TRUE(source.channel_state(2).last_sample_ms != 0);
    TEST_ASSERT_FLOAT_WITHIN(0.05f,
                             SimulatorAnalogInput::scripted_rail_volts(90000),
                             source.rail_volts());
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_divider_is_undone_exactly);
    RUN_TEST(test_two_point_fit_is_not_hardcoded_to_any_output_span);
    RUN_TEST(test_two_point_fit_handles_a_descending_sender);
    RUN_TEST(test_beta_model_hits_its_own_reference_point);
    RUN_TEST(test_steinhart_hart_matches_three_known_points);
    RUN_TEST(test_lut_interpolates_between_points_and_extrapolates_past_them);
    RUN_TEST(
        test_ratiometric_correction_holds_a_reading_through_a_sagging_rail);

    RUN_TEST(test_an_open_thermistor_leg_is_rejected_not_believed);
    RUN_TEST(test_the_band_follows_the_calibration_rather_than_a_constant);
    RUN_TEST(test_an_out_of_band_reading_produces_no_write);

    RUN_TEST(test_ema_step_response_matches_its_time_constant);
    RUN_TEST(test_time_constant_is_independent_of_sample_cadence);
    RUN_TEST(test_median_rejects_a_single_sample_spike);

    RUN_TEST(test_a_preset_fills_the_calibration_and_is_then_irrelevant);
    RUN_TEST(test_every_preset_is_self_consistent);

    RUN_TEST(test_conditioning_recovers_the_simulators_scripted_values);
    RUN_TEST(test_source_writes_conditioned_values_into_the_store);
    RUN_TEST(test_only_displayed_signals_cost_a_conversion);

    return UNITY_END();
}
