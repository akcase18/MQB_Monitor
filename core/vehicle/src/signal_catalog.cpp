/**
 * @file signal_catalog.cpp
 * @brief The 22 signals ported from the old Info/Request catalog
 * (src/logic/setup.cpp). Literal offsets/scales audited against that file;
 * two dead, unused requests (actual/specified lambda, both bugged with an
 * integer-division `(1 / 1024)` scale that always evaluated to 0) and one
 * unused turbo-speed request were never wired to an Info and are dropped
 * rather than ported, since nothing referenced them.
 */
#include "signal_catalog.hpp"

namespace core
{

    namespace
    {
        constexpr float NO_OFFSET = 0.0f;
        constexpr float NO_SCALE  = 1.0f;

        float transform_default(const float *raw_values, uint8_t n)
        {
            (void)n;
            return raw_values[0];
        }

        float transform_actual_intake_pressure(const float *raw_values,
                                               uint8_t      n)
        {
            (void)n;
            /* index 0 = ambient pressure, index 1 = intake pressure */
            return raw_values[1] - raw_values[0];
        }
    } // namespace

    const SignalSpec SIGNAL_CATALOG[] = {
        /* Coolant Temp */
        { 0,
          "Cool Temp",
          UnitType::Temp,
          2000,
          { { 0xF4, 0x05, -40.0f, NO_SCALE } },
          1,
          transform_default },

        /* Engine Oil Temp */
        { 1,
          "Eng Oil Temp",
          UnitType::Temp,
          1000,
          { { 0x40, 0xD5, NO_OFFSET, 0.1f } },
          1,
          transform_default },

        /* Intake Air Temp */
        { 2,
          "Int Air Temp",
          UnitType::Temp,
          100,
          { { 0xF4, 0x0F, -40.0f, NO_SCALE } },
          1,
          transform_default },

        /* Charge Pressure */
        { 3,
          "Charge Pres",
          UnitType::Pressure,
          100,
          { { 0x20, 0x2A, NO_OFFSET, 0.001f } },
          1,
          transform_default },

        /* Intake Pressure */
        { 4,
          "Int Pres",
          UnitType::Pressure,
          20,
          { { 0x3E, 0x70, NO_OFFSET, 0.001f } },
          1,
          transform_default },

        /* Ambient Pressure */
        { 5,
          "Amb Pres",
          UnitType::Pressure,
          1000,
          { { 0x13, 0xCA, NO_OFFSET, 0.0001f } },
          1,
          transform_default },

        /* Actual Intake Pressure (intake minus ambient) */
        { 6,
          "Int Pres Act.",
          UnitType::Pressure,
          20,
          { { 0x13, 0xCA, NO_OFFSET, 0.0001f },
            { 0x3E, 0x70, NO_OFFSET, 0.001f } },
          2,
          transform_actual_intake_pressure },

        /* Engine RPM */
        { 7,
          "Engine Speed",
          UnitType::RPM,
          20,
          { { 0x20, 0x6E, NO_OFFSET, NO_SCALE } },
          1,
          transform_default },

        /* Engine Torque */
        { 8,
          "Engine Torque",
          UnitType::Torque,
          20,
          { { 0x43, 0x7C, NO_OFFSET, 0.1f } },
          1,
          transform_default },

        /* Vehicle Speed */
        { 9,
          "Speed",
          UnitType::Speed,
          100,
          { { 0x20, 0x33, NO_OFFSET, 0.01f } },
          1,
          transform_default },

        /* Fuel Level (Volume) */
        { 10,
          "Fuel",
          UnitType::Volume,
          5000,
          { { 0x22, 0x06, NO_OFFSET, NO_SCALE } },
          1,
          transform_default },

        /* Fuel Level (Percentage) */
        { 11,
          "Fuel",
          UnitType::Percent,
          5000,
          { { 0xF4, 0x2F, NO_OFFSET, 1.0f / 2.55f } },
          1,
          transform_default },

        /* Throttle Position */
        { 12,
          "Throttle Pos",
          UnitType::Percent,
          20,
          { { 0xF4, 0x11, NO_OFFSET, 1.0f / 2.55f } },
          1,
          transform_default },

        /* Misfires (Sum) */
        { 13,
          "Misfire Cnt",
          UnitType::None,
          500,
          { { 0x29, 0x04, NO_OFFSET, NO_SCALE } },
          1,
          transform_default },

        /* Misfires (Cylinder 1-4) */
        { 14,
          "Misfire Cnt Cyl 1",
          UnitType::None,
          500,
          { { 0x29, 0x66, NO_OFFSET, NO_SCALE } },
          1,
          transform_default },
        { 15,
          "Misfire Cnt Cyl 2",
          UnitType::None,
          500,
          { { 0x29, 0x67, NO_OFFSET, NO_SCALE } },
          1,
          transform_default },
        { 16,
          "Misfire Cnt Cyl 3",
          UnitType::None,
          500,
          { { 0x29, 0x68, NO_OFFSET, NO_SCALE } },
          1,
          transform_default },
        { 17,
          "Misfire Cnt Cyl 4",
          UnitType::None,
          500,
          { { 0x29, 0x69, NO_OFFSET, NO_SCALE } },
          1,
          transform_default },

        /* Timing Correction (Cylinder 1-4) */
        { 18,
          "Time Corr. Cyl 1",
          UnitType::Degrees,
          100,
          { { 0x20, 0x0A, NO_OFFSET, 0.01f } },
          1,
          transform_default },
        { 19,
          "Time Corr. Cyl 2",
          UnitType::Degrees,
          100,
          { { 0x20, 0x0B, NO_OFFSET, 0.01f } },
          1,
          transform_default },
        { 20,
          "Time Corr. Cyl 3",
          UnitType::Degrees,
          100,
          { { 0x20, 0x0C, NO_OFFSET, 0.01f } },
          1,
          transform_default },
        { 21,
          "Time Corr. Cyl 4",
          UnitType::Degrees,
          100,
          { { 0x20, 0x0D, NO_OFFSET, 0.01f } },
          1,
          transform_default },
    };

    static_assert(
        sizeof(SIGNAL_CATALOG) / sizeof(SIGNAL_CATALOG[0])
            == SIGNAL_CATALOG_COUNT,
        "SIGNAL_CATALOG_COUNT must match SIGNAL_CATALOG's entry count");

    int signal_catalog_find(uint16_t id)
    {
        for (size_t i = 0; i < SIGNAL_CATALOG_COUNT; i++)
        {
            if (SIGNAL_CATALOG[i].id == id)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    const char *unit_label(UnitType type, bool is_metric)
    {
        switch (type)
        {
            case UnitType::Degrees:
                return "[°]";
            case UnitType::Percent:
                return "[%]";
            case UnitType::Pressure:
                return is_metric ? "[bar]" : "[psi]";
            case UnitType::RPM:
                return "[rpm]";
            case UnitType::Speed:
                return is_metric ? "[kph]" : "[mph]";
            case UnitType::Temp:
                return is_metric ? "[°C]" : "[°F]";
            case UnitType::Torque:
                return is_metric ? "[Nm]" : "[ft/lbs]";
            case UnitType::Volume:
                return is_metric ? "[L]" : "[gal]";
            case UnitType::None:
            default:
                return "";
        }
    }

    uint8_t unit_decimals(UnitType type, bool is_metric)
    {
        switch (type)
        {
            case UnitType::Pressure:
                return is_metric ? 2 : 1;
            case UnitType::Volume:
                return 1;
            default:
                return 0;
        }
    }

    float unit_convert_to_imperial(UnitType type, float metric_value)
    {
        switch (type)
        {
            case UnitType::Pressure:
                return metric_value * 14.5038f;
            case UnitType::Speed:
                return metric_value / 1.609f;
            case UnitType::Temp:
                return metric_value * 1.8f + 32.0f;
            case UnitType::Torque:
                return metric_value * 0.7375621493f;
            case UnitType::Volume:
                return metric_value / 3.785f;
            case UnitType::None:
            default:
                return metric_value;
        }
    }

} // namespace core
