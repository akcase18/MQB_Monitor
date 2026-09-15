#include "curve_library.hpp"

#include <string.h>

namespace core
{

    namespace
    {
        /**
         * The front end this product's board actually has: a 10k/15k
         * 0.1% pair taking a 5V-referenced sensor to 3.0V at the ADC. A
         * different board is a different pair of numbers in the form, not
         * a different preset. */
        constexpr float FRONT_R_TOP    = 10000.0f;
        constexpr float FRONT_R_BOTTOM = 15000.0f;

        /**
         * Pressure moves fast enough to be worth seeing move; the two
         * thermal masses physically cannot, so they get smoothed hard. */
        constexpr float TAU_PRESSURE_S = 0.25f;
        constexpr float TAU_TEMP_S     = 2.0f;

        /**
         * Nominal full scale for the pressure presets, in bar (the native
         * metric unit for UnitType::Pressure). ~10 bar is 145 psi, in the
         * middle of the automotive range -- but it is the one number that
         * must be corrected to match the part actually fitted, which is
         * what every pressure preset's note says. */
        constexpr float NOMINAL_FS_BAR = 10.0f;

        SensorCal blank()
        {
            SensorCal cal;
            memset(&cal, 0, sizeof(cal));
            cal.kind        = SensorKind::Unconfigured;
            cal.signal_id   = SENSOR_SIGNAL_NONE;
            cal.preset_id   = SENSOR_PRESET_CUSTOM;
            cal.band_margin = SENSOR_DEFAULT_BAND_MARGIN;
            return cal;
        }

        SensorCal linear_preset(uint16_t id, float v_lo, float v_hi)
        {
            SensorCal cal             = blank();
            cal.kind                  = SensorKind::LinearVoltage;
            cal.preset_id             = id;
            cal.divider.r_top         = FRONT_R_TOP;
            cal.divider.r_bottom      = FRONT_R_BOTTOM;
            cal.linear.v_lo           = v_lo;
            cal.linear.value_lo       = 0.0f;
            cal.linear.v_hi           = v_hi;
            cal.linear.value_hi       = NOMINAL_FS_BAR;
            cal.linear.ratiometric    = true;
            cal.linear.nominal_supply = 5.0f;
            cal.filter_tau_s          = TAU_PRESSURE_S;
            return cal;
        }

        SensorCal beta_preset(uint16_t id, float r25, float beta)
        {
            SensorCal cal               = blank();
            cal.kind                    = SensorKind::Thermistor;
            cal.preset_id               = id;
            cal.thermistor.pullup_ohms  = 2200.0f;
            cal.thermistor.supply_volts = 3.3f;
            cal.thermistor.model        = CurveModel::Beta;
            cal.thermistor.r25_ohms     = r25;
            cal.thermistor.beta         = beta;
            cal.thermistor.valid_min_c  = -40.0f;
            cal.thermistor.valid_max_c  = 150.0f;
            cal.filter_tau_s            = TAU_TEMP_S;
            return cal;
        }

        SensorCal rail_preset(uint16_t id)
        {
            SensorCal cal        = blank();
            cal.kind             = SensorKind::RailSense;
            cal.preset_id        = id;
            cal.divider.r_top    = FRONT_R_TOP;
            cal.divider.r_bottom = FRONT_R_BOTTOM;

            /**
             * An identity fit -- the line through (4.0, 4.0) and
             * (5.5, 5.5) has slope 1 and no offset -- chosen at those two
             * points rather than at 0 and 5 so that the plausibility band
             * derived from them is a useful statement about the rail. A
             * 5V rail outside 4.0-5.5V is a supply fault, and ratiometric
             * correction against it would make three good sensors read
             * wrong together. */
            cal.linear.v_lo     = 4.0f;
            cal.linear.value_lo = 4.0f;
            cal.linear.v_hi     = 5.5f;
            cal.linear.value_hi = 5.5f;

            cal.linear.ratiometric    = false;
            cal.linear.nominal_supply = 5.0f;
            cal.filter_tau_s          = TAU_PRESSURE_S;
            return cal;
        }

        /**
         * Built lazily into a function-local static rather than declared as
         * a file-scope const array, because each entry is composed by the
         * helpers above. One-time cost at first use; no static
         * initialization order to worry about. */
        const SensorPreset *table(uint8_t &count)
        {
            static SensorPreset s_table[] = {
                { 1,
                  SensorKind::LinearVoltage,
                  "0.5-4.5 V ratiometric",
                  "Most common automotive transducer. Set full scale to "
                  "match the part fitted.",
                  linear_preset(1, 0.5f, 4.5f) },
                { 2,
                  SensorKind::LinearVoltage,
                  "0-5 V ratiometric",
                  "No offset, so a broken wire reads as a valid zero -- "
                  "prefer an offset part where you can. Set full scale.",
                  linear_preset(2, 0.0f, 5.0f) },
                { 3,
                  SensorKind::LinearVoltage,
                  "1-5 V ratiometric",
                  "Wide offset; good open-circuit detection. Set full "
                  "scale.",
                  linear_preset(3, 1.0f, 5.0f) },
                { 4,
                  SensorKind::LinearVoltage,
                  "0.25-4.75 V ratiometric",
                  "Set full scale to match the part fitted.",
                  linear_preset(4, 0.25f, 4.75f) },

                { 10,
                  SensorKind::Thermistor,
                  "Generic NTC 10k, B=3435",
                  "Nominal curve, not a specific part. Verify against a "
                  "decade box, or enter the datasheet table as a LUT.",
                  beta_preset(10, 10000.0f, 3435.0f) },
                { 11,
                  SensorKind::Thermistor,
                  "Generic NTC 2.2k, B=3500",
                  "Nominal curve, not a specific part. Verify against a "
                  "decade box, or enter the datasheet table as a LUT.",
                  beta_preset(11, 2200.0f, 3500.0f) },
                { 12,
                  SensorKind::Thermistor,
                  "Generic NTC 5k, B=3892",
                  "Nominal curve, not a specific part. Verify against a "
                  "decade box, or enter the datasheet table as a LUT.",
                  beta_preset(12, 5000.0f, 3892.0f) },

                { 20,
                  SensorKind::RailSense,
                  "5 V rail via 10k/15k divider",
                  "Feeds ratiometric correction. Not displayed.",
                  rail_preset(20) },
            };

            count = static_cast<uint8_t>(sizeof(s_table) / sizeof(s_table[0]));
            return s_table;
        }
    } // namespace

    uint8_t sensor_preset_count()
    {
        uint8_t count = 0;
        (void)table(count);
        return count;
    }

    const SensorPreset &sensor_preset_at(uint8_t index)
    {
        uint8_t             count   = 0;
        const SensorPreset *entries = table(count);
        if (index >= count)
        {
            index = 0;
        }
        return entries[index];
    }

    const SensorPreset *sensor_preset_find(uint16_t id)
    {
        uint8_t             count   = 0;
        const SensorPreset *entries = table(count);
        for (uint8_t i = 0; i < count; i++)
        {
            if (entries[i].id == id)
            {
                return &entries[i];
            }
        }
        return nullptr;
    }

    bool sensor_preset_apply(uint16_t id, SensorCal &cal)
    {
        const SensorPreset *preset = sensor_preset_find(id);
        if (preset == nullptr)
        {
            return false;
        }

        /**
         * Wiring survives; the part description does not. Which channel a
         * sensor is on and which gauge it drives are facts about the
         * harness, and picking a different sender does not move a wire. */
        uint8_t  channel   = cal.channel;
        uint16_t signal_id = cal.signal_id;

        cal           = preset->cal;
        cal.channel   = channel;
        cal.signal_id = signal_id;
        cal.preset_id = id;

        return true;
    }

    SensorCal sensor_cal_default(SensorKind kind)
    {
        SensorCal cal = blank();
        cal.kind      = kind;

        switch (kind)
        {
            case SensorKind::LinearVoltage:
            case SensorKind::RailSense:
                cal.filter_tau_s          = TAU_PRESSURE_S;
                cal.linear.nominal_supply = 5.0f;
                break;

            case SensorKind::Thermistor:
                cal.filter_tau_s            = TAU_TEMP_S;
                cal.thermistor.supply_volts = 3.3f;
                cal.thermistor.model        = CurveModel::Beta;
                break;

            case SensorKind::Unconfigured:
                break;
        }

        return cal;
    }

} // namespace core
