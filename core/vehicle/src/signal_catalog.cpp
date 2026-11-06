/**
 * @file signal_catalog.cpp
 * @brief Home of the transform_* functions and unit helpers for the signal
 * catalog. The SIGNAL_CATALOG[] array itself is GENERATED from
 * core/vehicle/data/signals.json -- the canonical catalog shared with the
 * Mac monitor tool (tools/monitor) -- into signal_catalog.generated.inc by
 * tools/gen_catalog.py. Edit signals.json and re-run that script to change a
 * signal; add a new transform here (and reference it by name in the JSON's
 * "transform" field) to add non-default combining behavior.
 *
 * History: the 22 signals were ported from the old Info/Request catalog
 * (src/logic/setup.cpp). Two dead, unused requests (actual/specified lambda,
 * both bugged with an integer-division `(1 / 1024)` scale that always
 * evaluated to 0) and one unused turbo-speed request were never wired to an
 * Info and are dropped rather than ported.
 */
#include "signal_catalog.hpp"

#include <stdio.h>
#include <string.h>

#include "mem_placement.hpp"

namespace core
{

    namespace
    {
        float transform_default(const float *raw_values, uint8_t n)
        {
            (void)n;
            return raw_values[0];
        }

        /**
         * Generic two-request combiners for compound signals built via the
         * monitor tool's Registry "New Compound Signal..." dialog -- no new
         * C++ needed per combination.
         */
        float transform_sum(const float *raw_values, uint8_t n)
        {
            (void)n;
            return raw_values[0] + raw_values[1];
        }

        float transform_difference(const float *raw_values, uint8_t n)
        {
            (void)n;
            return raw_values[1] - raw_values[0];
        }

        float transform_average(const float *raw_values, uint8_t n)
        {
            (void)n;
            return (raw_values[0] + raw_values[1]) * 0.5f;
        }

        float transform_min(const float *raw_values, uint8_t n)
        {
            (void)n;
            return raw_values[0] < raw_values[1] ? raw_values[0]
                                                 : raw_values[1];
        }

        float transform_max(const float *raw_values, uint8_t n)
        {
            (void)n;
            return raw_values[0] > raw_values[1] ? raw_values[0]
                                                 : raw_values[1];
        }
    } // namespace

#include "signal_catalog.generated.inc"

    static_assert(
        sizeof(SIGNAL_CATALOG) / sizeof(SIGNAL_CATALOG[0])
            == SIGNAL_TABLE_COUNT,
        "SIGNAL_CATALOG_COUNT + INTERNAL_SIGNAL_COUNT must match "
        "SIGNAL_CATALOG's entry count");

    const char *signal_enum_label(const SignalSpec &spec, float value)
    {
        int32_t n = static_cast<int32_t>(value < 0.0f ? value - 0.5f
                                                      : value + 0.5f);
        for (uint8_t i = 0; i < spec.num_enum_entries; i++)
        {
            if (spec.enum_entries[i].value == n)
            {
                return spec.enum_entries[i].label;
            }
        }
        return nullptr;
    }

    /**
     * FLASHMEM: runs once, at boot.
     */
    FLASHMEM void uds_ecu_rx_range(uint32_t &lo, uint32_t &hi)
    {
        lo = UDS_ECUS[0].rx_id;
        hi = lo;
        for (uint8_t i = 1; i < UDS_ECU_COUNT; i++)
        {
            lo = UDS_ECUS[i].rx_id < lo ? UDS_ECUS[i].rx_id : lo;
            hi = UDS_ECUS[i].rx_id > hi ? UDS_ECUS[i].rx_id : hi;
        }
    }

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

    /**
     * Off the hot path -- the poller's table rebuild and a few id-keyed
     * lookups -- so flash, unlike signal_catalog_find().
     */
    FLASHMEM int signal_table_find(uint16_t id)
    {
        for (size_t i = 0; i < SIGNAL_TABLE_COUNT; i++)
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
            case UnitType::Angle:
                return "°";
            case UnitType::Percent:
                return "%";
            case UnitType::Pressure:
                return is_metric ? "bar" : "psi";
            case UnitType::RPM:
                return "rpm";
            case UnitType::Speed:
                return is_metric ? "kph" : "mph";
            case UnitType::Temp:
                return is_metric ? "°C" : "°F";
            case UnitType::Torque:
                return is_metric ? "Nm" : "lb-ft";
            case UnitType::Volume:
                return is_metric ? "L" : "gal";
            case UnitType::Length:
                return is_metric ? "mm" : "in";
            case UnitType::Voltage:
                return "V";
            case UnitType::Current:
                return "A";
            case UnitType::Illuminance:
                return "lx";
            case UnitType::Time:
                return "min";
            case UnitType::TempDelta:
                return is_metric ? "°C" : "°F";
            case UnitType::Enum:
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
            case UnitType::Length:
                return is_metric ? 0 : 2;
            case UnitType::Voltage:
                return 2;
            case UnitType::Current:
                return 1;
            default:
                return 0;
        }
    }

    float warning_step(UnitType type)
    {
        switch (type)
        {
            case UnitType::Pressure:
                return 0.1f;
            case UnitType::RPM:
                return 50.0f;
            case UnitType::Speed:
                return 5.0f;
            case UnitType::Torque:
                return 10.0f;
            case UnitType::None:
                return 5.0f;
            case UnitType::Voltage:
                return 0.1f;
            case UnitType::Current:
                return 0.5f;
            case UnitType::Illuminance:
                return 10.0f;
            case UnitType::Temp:
            case UnitType::Angle:
            case UnitType::Percent:
            case UnitType::Volume:
            default:
                return 1.0f;
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
            case UnitType::Length:
                return metric_value / 25.4f;
            case UnitType::TempDelta:
                return metric_value * 1.8f;
            case UnitType::None:
            default:
                return metric_value;
        }
    }

    float unit_convert_to_metric(UnitType type, float imperial_value)
    {
        switch (type)
        {
            case UnitType::Pressure:
                return imperial_value / 14.5038f;
            case UnitType::Speed:
                return imperial_value * 1.609f;
            case UnitType::Temp:
                return (imperial_value - 32.0f) / 1.8f;
            case UnitType::Torque:
                return imperial_value / 0.7375621493f;
            case UnitType::Volume:
                return imperial_value * 3.785f;
            case UnitType::Length:
                return imperial_value * 25.4f;
            case UnitType::TempDelta:
                return imperial_value / 1.8f;
            case UnitType::None:
            default:
                return imperial_value;
        }
    }

    DisplayRange resolve_display_range(const SignalSpec &spec,
                                       bool              use_custom_range,
                                       float             custom_min,
                                       float             custom_max)
    {
        if (use_custom_range && custom_min < custom_max)
        {
            return { custom_min, custom_max };
        }

        return { spec.display_min, spec.display_max };
    }

} // namespace core
