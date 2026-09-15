/**
 * @file conditioning.hpp
 * @brief Volts at an ADC pin -> a number in engineering units, or a reason
 * not to believe one.
 *
 * Every function here is pure: same inputs, same output, no state, no
 * clock, no hardware. That is what makes the maths testable on the host
 * against known inputs, which matters more here than almost anywhere else
 * in the codebase -- a wrong number on an oil pressure gauge comes from
 * exactly these forty lines of arithmetic.
 *
 * The status returned alongside a value is not an error code in the usual
 * sense. With UDS, a dead sensor produced no response and `has_data`
 * stayed false on its own. With analog, **a broken wire produces a
 * confident number**: an open thermistor leg reads the pull-up rail, which
 * converts cleanly to about -86 C. Nothing throws, nothing times out, and
 * the gauge shows a plausible-looking value that is entirely fiction. The
 * band check is the only thing standing between that and the driver, which
 * is why it is derived from the sensor's own configured span rather than
 * from a constant somebody has to remember to update.
 */
#pragma once

#include "sensor_cal.hpp"

namespace core
{

    /**
     * Why a reading is or is not usable.
     *
     * Only `Ok` may reach SignalStore::set_at(). Everything else means "do
     * not write", which leaves `has_data` false and blanks the panel and
     * suppresses the warning through machinery that already exists.
     */
    enum class SensorStatus : uint8_t
    {
        Ok = 0,
        Unconfigured, /* kind == Unconfigured. */
        BadCal,       /**
                       * The calibration itself is unusable -- a zero-width
                       * voltage span, an empty LUT, a non-positive R25. */
        OpenCircuit,  /* Thermistor node sitting at the supply rail. */
        ShortCircuit, /* Thermistor node sitting at ground. */
        BelowRange,   /* Plausible arithmetic, implausibly low result. */
        AboveRange
    };

    struct SensorReading
    {
            float        value;
            SensorStatus status;
    };

    /** The span of readings a calibration claims to cover, in the signal's
     * native metric unit. `valid` is false for kinds that have no span
     * (Unconfigured, RailSense). */
    struct SensorSpan
    {
            float min;
            float max;
            bool  valid;
    };

    /** Short human-readable form, for the config page's live channel
     * readout. Static storage; never null. */
    const char *sensor_status_text(SensorStatus status);

    /** True only for SensorStatus::Ok. Named so call sites read as intent
     * rather than as an equality check somebody could invert. */
    inline bool sensor_reading_is_usable(SensorStatus status)
    {
        return status == SensorStatus::Ok;
    }

    /**
     * --------------------------------------------------------------------
     * The chain, in pieces. Exposed individually because each one is worth
     * testing on its own, and because the config page shows intermediate
     * values (raw volts, computed resistance) next to the final number
     * while somebody is calibrating.
     *-------------------------------------------------------------------*/

    /**
     * Undoes a resistive divider. Returns `v_adc` unchanged when the
     * divider is not configured, so a channel with no divider needs no
     * special case anywhere.
     */
    float divider_source_volts(const Divider &divider, float v_adc);

    /**
     * Applies a two-point fit. Nothing about any particular sensor's
     * output span is baked in; `cal` carries all four numbers.
     */
    float linear_apply(const LinearCal &cal, float v_source);

    /**
     * Resistance of the lower leg of a pull-up divider.
     *
     * @returns Negative on an open circuit (node at or above the supply,
     * where the arithmetic diverges) -- the caller must check rather than
     * letting an infinity propagate into a temperature.
     */
    float ntc_resistance(const ThermistorCal &cal, float v_node);

    /* R -> C by each model. All three assume `ohms` > 0. */
    float ntc_temp_beta(const ThermistorCal &cal, float ohms);
    float ntc_temp_steinhart(const ThermistorCal &cal, float ohms);

    /**
     * Interpolates the hand-entered table.
     *
     * Works in (ln R, 1/T) space rather than straight lines between
     * points, because that is the space an NTC is actually linear in --
     * the Beta model is exactly a straight line there. So a table sampled
     * from a curve reproduces that curve between its points instead of
     * chording across it, and sixteen points comfortably covers -40..150 C.
     *
     * Beyond either end it extrapolates along the last segment rather than
     * clamping. Clamping would manufacture a plausible reading out of a
     * sensor that has left its calibrated range -- exactly the failure this
     * module exists to prevent. Extrapolated values run out of the
     * plausibility band on their own.
     */
    float ntc_temp_lut(const ThermistorCal &cal, float ohms);

    /** Dispatches on `cal.model`. */
    float ntc_temp(const ThermistorCal &cal, float ohms);

    /**
     * --------------------------------------------------------------------
     * The whole chain
     *-------------------------------------------------------------------*/

    /**
     * ADC volts -> engineering units, with the plausibility verdict.
     *
     * @param cal        What is wired to this channel.
     * @param v_adc      Measured voltage at the ADC pin.
     * @param rail_volts Measured supply, for ratiometric sensors. Pass <= 0
     *                   when no rail-sense channel is configured, and
     *                   `cal.linear.nominal_supply` is assumed instead.
     */
    SensorReading sensor_convert(const SensorCal &cal,
                                 float            v_adc,
                                 float            rail_volts);

    /**
     * The span `cal` claims to measure.
     *
     * Two things read this, and their sharing it is the point (§1.2):
     * the plausibility band widens it by `band_margin`, and saving a
     * calibration seeds the gauge's display range from it. So swapping to
     * a different sensor moves what the bar spans and what counts as a
     * fault together, with no firmware change and nothing for the user to
     * keep in sync by hand.
     */
    SensorSpan sensor_cal_span(const SensorCal &cal);

} // namespace core
