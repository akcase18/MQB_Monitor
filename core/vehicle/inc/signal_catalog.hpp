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

            /**
             * Full-scale bounds for this signal, in the same native
             * (metric) unit its raw value is stored/compared in — scales a
             * Bar/Gauge panel's fill and bounds the warning-threshold
             * editor's +/- buttons. Sensible engineering defaults, not a
             * per-vehicle-tuned range; unbounded counters (misfires) get a
             * generous, arbitrary cap. A user can override these per
             * signal (core::config::SignalConfig::use_custom_range) —
             * always resolve the *effective* range via
             * resolve_display_range() below rather than reading these
             * fields directly.
             */
            float display_min;
            float display_max;
    };

    extern const SignalSpec SIGNAL_CATALOG[];
    inline constexpr size_t SIGNAL_CATALOG_COUNT = 22;

    /** A signal's effective display range — either SignalSpec's compiled-in
     * default or a user-calibrated override. */
    struct DisplayRange
    {
            float min;
            float max;
    };

    /**
     * Resolves `spec`'s effective display range: `spec.display_min/max`, or
     * `custom_min/custom_max` if `use_custom_range` is set. Takes plain
     * values rather than a core::config::SignalConfig so core/vehicle stays
     * ignorant of core/config's existence — the same relationship
     * evaluate_warning() already has to WarningThresholds. If
     * `custom_min >= custom_max`, falls back to the catalog default rather
     * than returning a degenerate range.
     */
    DisplayRange resolve_display_range(const SignalSpec &spec,
                                       bool              use_custom_range,
                                       float             custom_min,
                                       float             custom_max);

    /**
     * @returns The catalog index for `id`, or -1 if not found.
     */
    int signal_catalog_find(uint16_t id);

    /**
     * Writes SIGNAL_CATALOG[index]'s name as a signal list shows it into
     * `buf`: the name, plus " (<unit_tag>)" when another signal shares it
     * (Fuel by volume and in %). Truncates to fit `len`.
     */
    void signal_catalog_label(size_t index, char *buf, size_t len);

    /**
     * @returns The unit label to display alongside a signal's value, e.g.
     * "[bar]"/"[psi]" for Pressure depending on `is_metric`.
     */
    const char *unit_label(UnitType type, bool is_metric);

    /**
     * @returns A short name for the unit type that reads the same in either
     * unit system ("vol", "temp", "%"), for telling apart signals that share
     * a name — see signal_catalog_label(). Mirrored by the phone
     * configurator's UNITS table (platform/esp32/web/index.html).
     */
    const char *unit_tag(UnitType type);

    /**
     * @returns How many decimal places to display a value with, for the given
     * unit type/system.
     */
    uint8_t unit_decimals(UnitType type, bool is_metric);

    /**
     * @returns The +/- step size for adjusting this unit type's warning
     * thresholds (see SignalSpec::display_min/max) — one tap/repeat-tick's
     * worth of adjustment, in the same native (metric) unit.
     */
    float warning_step(UnitType type);

    /**
     * Converts a metric value to imperial for the given unit type. No-op for
     * unit types with no imperial conversion (None, Degrees, Percent, RPM).
     */
    float unit_convert_to_imperial(UnitType type, float metric_value);

    /**
     * Inverse of unit_convert_to_imperial — converts an imperial value back
     * to the signal's native (metric) storage/comparison unit. Needed by
     * the warning-threshold editor, which edits in whichever unit system
     * the panel currently displays but must persist/compare in metric.
     */
    float unit_convert_to_metric(UnitType type, float imperial_value);

} // namespace core
