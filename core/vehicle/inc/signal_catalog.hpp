/**
 * @file signal_catalog.hpp
 * @brief Static, ROM-able metadata for every vehicle signal the dashboard
 * can display: what it is called, what it measures, and how to present the
 * result. Zero LVGL/Arduino/platform deps -- and, deliberately, zero
 * knowledge of how a value is acquired.
 *
 * How a signal is *obtained* lives outside this file, in a per-product
 * acquisition table keyed by signal id: core/can's uds_binding.hpp for the
 * CAN product. That split is what lets everything which includes this
 * header -- core/config's persisted schema, gui/bindings, the web config
 * renderer -- stay independent of any particular bus or sensor.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

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

    struct SignalSpec
    {
            uint16_t    id;
            const char *name;
            UnitType    unit_type;

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

    /**
     * SIGNAL_CATALOG_COUNT. Its own generated file, selected by include
     * path, because the two products this tree builds share no signals at
     * all and a single hand-declared count cannot describe both. See
     * tools/gen_catalog.py; platformio.ini puts exactly one product's
     * generated directory on the include path per environment. */
#include "signal_catalog_count.inc"

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
