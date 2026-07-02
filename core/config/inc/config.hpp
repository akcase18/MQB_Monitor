/**
 * @file config.hpp
 * @brief Persisted configuration schema, stored over IConfigStore. Warning
 * thresholds are keyed by signal (Config.signals[], indexed by catalog
 * position) rather than by panel slot, so moving a signal to a different
 * panel carries its thresholds with it automatically.
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
    };

    struct PanelConfig
    {
            uint16_t   signal_id;
            UnitSystem units;
    };

    struct SignalConfig
    {
            WarningThresholds warn;
    };

    struct Config
    {
            ConfigHeader header;
            Settings     settings;
            PanelConfig  panels[MAX_PANELS];
            SignalConfig signals[SIGNAL_CATALOG_COUNT];
    };

} // namespace core
