/**
 * @file sensor_cal.hpp
 * @brief What is wired to an ADC channel, as plain data.
 *
 * This is the whole electrical description of one sensor: the divider in
 * front of it, its transfer function, how fast to filter it, and what range
 * of readings is physically believable. Everything else in core/sensors is
 * math over one of these.
 *
 * Three properties are deliberate, and all three exist to serve the same
 * goal -- that changing sensor is a form entry, never a rebuild:
 *
 * 1. **Flat POD, no pointers, no heap.** It can be memcpy'd into the
 *    persisted config blob and filled field-by-field from an HTTP form
 *    with no marshalling layer in between.
 *
 * 2. **Self-contained.** Nothing here is an index into a table the firmware
 *    has to still have later. A saved SensorCal carries every number needed
 *    to read its sensor, so a firmware update that drops or renames a
 *    built-in preset cannot change what a configured gauge displays. See
 *    `preset_id` for the one exception, which is a label and never math.
 *
 * 3. **The LUT is a complete escape hatch.** Any NTC whose datasheet
 *    publishes an R/T table can be entered by hand and be exactly right,
 *    without a curve model existing for it in firmware.
 *
 * Together those mean the two obvious web-form designs -- "pick your sensor
 * from a list" and "type in your own curve" -- are not alternatives to
 * choose between. A preset is just a button that fills this struct in
 * (see curve_library.hpp); manual entry fills the same fields directly, and
 * editing a field after picking a preset is a first-class thing to do
 * rather than a mode switch.
 */
#pragma once

#include <stdint.h>

namespace core
{

    /** How many (resistance, temperature) points a hand-entered curve may
     * carry. Sixteen covers the -40..150 C span published tables typically
     * use with room to spare, at 128 bytes per channel. */
    constexpr uint8_t SENSOR_LUT_MAX_POINTS = 16;

    /**
     * How many ADC channels a board may describe.
     *
     * Two ADS1115s' worth. The second is an address-strap change rather
     * than a board respin, so the ceiling is set at what the hardware can
     * reach and not at the four channels this product's first build
     * populates.
     *
     * It lives here, next to the struct it sizes, because it is a property
     * of the *calibration data* -- core::DeviceConfig persists
     * SENSOR_MAX_CHANNELS of these, and raising it later would be a schema
     * migration. core::AnalogSignalSource takes its own ceiling from this
     * one so the two cannot disagree.
     */
    constexpr uint8_t SENSOR_MAX_CHANNELS = 8;

    /** Marks a channel that is measured but never displayed -- the rail
     * sense. Not a valid catalog id. */
    constexpr uint16_t SENSOR_SIGNAL_NONE = 0xFFFF;

    /** Fraction of a sensor's own configured span allowed outside it before
     * a reading is called implausible (§4.4). Five percent is wide enough
     * to absorb divider tolerance and ADC offset, far too narrow to let a
     * disconnected wire through. */
    constexpr float SENSOR_DEFAULT_BAND_MARGIN = 0.05f;

    enum class SensorKind : uint8_t
    {
        Unconfigured = 0, /* Channel is not wired to anything. */
        LinearVoltage,    /* Any sensor whose output is linear in volts. */
        Thermistor,       /* An NTC against a pull-up. */
        RailSense         /**
                           * Measures a supply rail; feeds ratiometric
                           * correction rather than a displayed signal. */
    };

    /** Which model turns resistance into temperature. All three are
     * supported because they cost almost nothing and cover the whole range
     * from "I have two numbers" to "I have the datasheet's table". */
    enum class CurveModel : uint8_t
    {
        Beta = 0,      /* R25 + beta. Two numbers, running the same day. */
        SteinhartHart, /* A/B/C. Wide span, needs three known points. */
        Lut            /* Up to SENSOR_LUT_MAX_POINTS (R,T) pairs. */
    };

    /**
     * A resistive divider in front of the ADC pin, undone before anything
     * else happens. `r_top` <= 0 or `r_bottom` <= 0 means no divider --
     * which is the normal case for a thermistor channel, whose pull-up
     * already keeps the node inside the ADC's range.
     */
    struct Divider
    {
            float r_top;
            float r_bottom;
    };

    /**
     * A two-point transfer function, and nothing more specific than that.
     *
     * There is deliberately no "0.5-4.5V pressure sensor" type in this
     * firmware. A 0-5V sender, a 1-5V sender and a 0.25-4.75V sender are
     * the same code and different numbers, which is what makes swapping to
     * a part chosen after the board was built a configuration change.
     *
     * `value_lo`/`value_hi` are in the signal's native metric unit -- bar
     * for Pressure, C for Temp -- because that is what SignalStore holds
     * and what warning thresholds compare against. Imperial is a
     * display-time conversion that already exists.
     */
    struct LinearCal
    {
            float v_lo;
            float value_lo;
            float v_hi;
            float value_hi;

            /**
             * True for a sensor whose output is a fraction of its own
             * supply rather than an absolute voltage -- which is nearly
             * every automotive pressure transducer. A 5V rail sagging to
             * 4.8V under load moves an uncorrected reading by 4%.
             *
             * When set, the measured voltage is scaled by
             * `nominal_supply / measured_rail` before the fit is applied,
             * using whichever channel is a SensorKind::RailSense. With no
             * rail channel configured, `nominal_supply` is assumed and the
             * correction is a no-op.
             */
            bool  ratiometric;
            float nominal_supply;
    };

    /**
     * An NTC thermistor measured against a pull-up to a known supply.
     *
     * `valid_min_c`/`valid_max_c` are what the fitted curve is claimed to
     * cover, and they are load-bearing rather than documentation: the
     * plausibility band (§4.4) is derived from them, so fitting a different
     * sender moves the fault window with it automatically. Set them to the
     * span of the datasheet table the coefficients came from.
     */
    struct ThermistorCal
    {
            float pullup_ohms;
            float supply_volts;

            CurveModel model;

            /* CurveModel::Beta */
            float r25_ohms;
            float beta;

            /* CurveModel::SteinhartHart: 1/T = A + B*ln(R) + C*ln(R)^3 */
            float sh_a;
            float sh_b;
            float sh_c;

            /**
             * CurveModel::Lut. Sorted by ascending temperature, which for
             * an NTC means descending resistance. */
            float   lut_ohms[SENSOR_LUT_MAX_POINTS];
            float   lut_temp_c[SENSOR_LUT_MAX_POINTS];
            uint8_t lut_count;

            float valid_min_c;
            float valid_max_c;
    };

    /**
     * One ADC channel, described completely.
     *
     * All three variant bodies are stored side by side rather than in a
     * union. That costs about 30 bytes a channel and buys something worth
     * more: a form can round-trip fields the current `kind` does not use,
     * so toggling a channel from thermistor to linear and back does not
     * silently destroy the curve that was already entered.
     */
    struct SensorCal
    {
            SensorKind kind;

            /** ADC channel index this describes. */
            uint8_t channel;

            /** Catalog id this channel feeds, or SENSOR_SIGNAL_NONE for a
             * rail sense. Which channel drives which gauge is configuration
             * too -- nothing above platform/ hardcodes a channel number. */
            uint16_t signal_id;

            /**
             * Which built-in preset these numbers were last loaded from, or
             * SENSOR_PRESET_CUSTOM. **Provenance only** -- no conversion
             * ever reads it. It exists so the web form can say "Bosch
             * generic NTC (edited)" instead of "Custom", which is a real
             * usability difference and zero risk, precisely because
             * nothing depends on the preset still existing.
             */
            uint16_t preset_id;

            Divider       divider;
            LinearCal     linear;
            ThermistorCal thermistor;

            /** EMA time constant, seconds. 0 disables filtering. */
            float filter_tau_s;

            /** Plausibility margin as a fraction of the sensor's own span;
             * see SENSOR_DEFAULT_BAND_MARGIN. */
            float band_margin;
    };

} // namespace core
