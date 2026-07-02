/**
 * @file signal_catalog.hpp
 * @brief Static, ROM-able metadata for every vehicle signal the dashboard
 * can display: which UDS requests feed it, how to combine their decoded
 * values, and how to present the result. Zero LVGL/Arduino/platform deps.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "uds_client.hpp"

namespace core
{

    enum class UnitType
    {
        None,
        Degrees,
        Percent,
        Pressure,
        RPM,
        Speed,
        Temp,
        Torque,
        Volume
    };

    constexpr uint8_t MAX_REQUESTS_PER_SIGNAL = 2;

    struct SignalSpec
    {
            uint16_t       id;
            const char    *name;
            UnitType       unit_type;
            uint16_t       cycle_time_ms;
            UdsRequestSpec requests[MAX_REQUESTS_PER_SIGNAL];
            uint8_t        num_requests;

            /**
             * Combines this signal's decoded, per-request raw values (already
             * offset/scaled) into the signal's displayed value.
             */
            float (*transform)(const float *raw_values, uint8_t n);
    };

    extern const SignalSpec SIGNAL_CATALOG[];
    inline constexpr size_t SIGNAL_CATALOG_COUNT = 22;

    /**
     * @returns The catalog index for `id`, or -1 if not found.
     */
    int signal_catalog_find(uint16_t id);

    /**
     * @returns The unit label to display alongside a signal's value, e.g.
     * "[bar]"/"[psi]" for Pressure depending on `is_metric`.
     */
    const char *unit_label(UnitType type, bool is_metric);

    /**
     * @returns How many decimal places to display a value with, for the given
     * unit type/system.
     */
    uint8_t unit_decimals(UnitType type, bool is_metric);

    /**
     * Converts a metric value to imperial for the given unit type. No-op for
     * unit types with no imperial conversion (None, Degrees, Percent, RPM).
     */
    float unit_convert_to_imperial(UnitType type, float metric_value);

} // namespace core
