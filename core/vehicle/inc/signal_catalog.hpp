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

    /**
     * Ordinals go over the wire to the phone configurator (its UNITS
     * table, platform/esp32/web/index.html) -- append only, never reorder.
     * Same order as the monitor tool's UNITS (tools/monitor).
     */
    enum class UnitType
    {
        None,
        Angle,
        Percent,
        Pressure,
        RPM,
        Speed,
        Temp,
        Torque,
        Volume,
        Length,      /* mm / in */
        Voltage,     /* V */
        Current,     /* A */
        Illuminance, /* lx */
        Time,        /* min */
        TempDelta,   /* a temperature difference: °C / °F with no +32 */
        Enum         /* a state: shown as its EnumEntry label, no unit */
    };

    /**
     * One value -> label pair of an Enum signal (gear, valve state).
     */
    struct EnumEntry
    {
            int32_t     value;
            const char *label;
    };

    /**
     * Longest Enum label a panel can show -- gui::PANEL_VALUE_LEN - 1.
     * tools/gen_catalog.py refuses longer ones.
     */
    constexpr uint8_t ENUM_LABEL_MAX = 15;

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

            /**
             * UnitType::Enum only: the value -> label table, else
             * nullptr/0.
             */
            const EnumEntry *enum_entries;
            uint8_t          num_enum_entries;
    };

    /**
     * True for a signal shown as a state label rather than a number:
     * Digital panels only, no warning thresholds.
     */
    inline bool signal_is_state(const SignalSpec &spec)
    {
        return spec.unit_type == UnitType::Enum;
    }

    /**
     * @returns The label `spec` shows for `value` (rounded to the nearest
     * whole number), or nullptr when the value has no entry -- callers show
     * "?<n>" for that.
     */
    const char *signal_enum_label(const SignalSpec &spec, float value);

    /**
     * Every compiled signal: the SIGNAL_CATALOG_COUNT the user can pick
     * (panels, the signal picker, the phone page, the saved per-signal
     * config), then INTERNAL_SIGNAL_COUNT the firmware reads for itself
     * and never offers -- signals.json's "internal" status. Anything that
     * walks the user-facing catalog stops at SIGNAL_CATALOG_COUNT and
     * looks ids up with signal_catalog_find(); only the value store and
     * the poller, which serve both, go up to SIGNAL_TABLE_COUNT.
     */
    extern const SignalSpec SIGNAL_CATALOG[];

    /**
     * Every module the catalog's requests go to (UdsRequestSpec::ecu
     * indexes it), generated alongside SIGNAL_CATALOG.
     */
    extern const UdsEcu  UDS_ECUS[];
    extern const uint8_t UDS_ECU_COUNT;

    /**
     * The lowest and highest response id in UDS_ECUS -- what the CAN bus is
     * opened to receive (ICanBus::begin()).
     */
    void uds_ecu_rx_range(uint32_t &lo, uint32_t &hi);

    /* Both synced by tools/gen_catalog.py. */
    inline constexpr size_t SIGNAL_CATALOG_COUNT = 50;
    inline constexpr size_t INTERNAL_SIGNAL_COUNT = 1;
    inline constexpr size_t SIGNAL_TABLE_COUNT
        = SIGNAL_CATALOG_COUNT + INTERNAL_SIGNAL_COUNT;

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
     * @returns The catalog index for `id`, or -1 if not found. User-facing
     * signals only: an internal signal's id is not found.
     */
    int signal_catalog_find(uint16_t id);

    /**
     * @returns The SIGNAL_CATALOG index for `id`, internal signals
     * included, or -1 if not found. For the value store and the poller.
     */
    int signal_table_find(uint16_t id);

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
     * unit types with no imperial conversion (None, Angle, Percent, RPM).
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
