/**
 * @file config.hpp
 * @brief Persisted configuration schema, stored over IConfigStore. Warning
 * thresholds are keyed by signal (Config.signals[], indexed by catalog
 * position) rather than by panel slot, so moving a signal to a different
 * panel carries its thresholds with it automatically. Config.priorities[]
 * follows the same principle one level further: it's indexed by priority
 * rank (0 = most important), not screen slot, so the most important signal
 * stays the most prominent one on screen regardless of how many panels are
 * currently shown — resolve rank -> slot via
 * gui::compute_prominence_ranks() for the current panel count/orientation.
 */
#pragma once

#include <stdint.h>

#include "signal_catalog.hpp"
#include "warning.hpp"

namespace core
{

    constexpr uint8_t MAX_PANELS = 6;

    enum class UnitSystem
    {
        Metric,
        Imperial
    };

    enum class LayoutOrientation
    {
        Horizontal,
        Vertical
    };

    /** How a panel presents its assigned signal — kept in core:: (mirrored
     * by gui::DisplayMode, translated at the gui/bindings boundary like
     * UnitSystem/is_metric already are) since this is persisted config,
     * not a gui:: concept. */
    enum class DisplayMode
    {
        Digital,
        Bar,
        Gauge
    };

    struct ConfigHeader
    {
            uint16_t magic;
            uint8_t  version;
    };

    struct Settings
    {
            uint8_t day_brightness;
            uint8_t night_brightness;
            uint8_t num_panels; /* 1-6, active count within MAX_PANELS */
            LayoutOrientation orientation;
            /* Whether core::Logger records anything. Opt-in: logging is
             * a deliberate "capture this drive" action, not something
             * that should burn flash write-cycles on every commute, so
             * this defaults off and is toggled from SettingsScreen. */
            bool logging_enabled;
            /* Packed 0xRRGGBB — one of gui::ACCENT_PRESETS, picked via
             * SettingsScreen's Theme row. Stored as a raw value rather than
             * an lv_color_t so core:: stays LVGL-free; gui/bindings
             * converts it at the boundary. */
            uint32_t accent_color;
    };

    /** A single priority-ranked signal assignment — see the file doc
     * comment on Config.priorities[] for what the index means. */
    struct PrioritizedSignal
    {
            uint16_t    signal_id;
            UnitSystem  units;
            DisplayMode display_mode = DisplayMode::Digital;
    };

    struct SignalConfig
    {
            WarningThresholds warn;
            /* Per-vehicle override of SignalSpec::display_min/max — resolve
             * through core::resolve_display_range() rather than reading
             * these directly. */
            bool  use_custom_range = false;
            float custom_min       = 0.0f;
            float custom_max       = 0.0f;
    };

    struct Config
    {
            ConfigHeader      header;
            Settings          settings;
            PrioritizedSignal priorities[MAX_PANELS];
            SignalConfig      signals[SIGNAL_CATALOG_COUNT];
    };

} // namespace core
