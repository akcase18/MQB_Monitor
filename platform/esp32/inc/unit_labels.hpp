/**
 * @file unit_labels.hpp
 * @brief Small presentation-layer tables the phone config form
 * (config_web.hpp) needs, deliberately duplicated here rather than linking
 * core::vehicle/gui::theme into platform/esp32 (which Phase 4 kept free of
 * any core::/gui::/remote::server dependency — see platformio.ini's
 * env:esp32c3). unit_label()/unit_convert_to_imperial()/
 * unit_convert_to_metric() mirror core::vehicle::signal_catalog.cpp's
 * actual implementations (not always its doc comments, which have drifted
 * in places — e.g. unit_label()'s header comment says "[bar]"/"[psi]" but
 * the real implementation has no brackets); ACCENT_PRESETS mirrors
 * gui::theme::theme.cpp's actual 9 values. `unit_type` parameters below
 * are the raw wire byte from SignalCatalogEntryWire::unit_type, matching
 * core::UnitType's enum ordinal (None=0 .. Volume=8) exactly as
 * remote_config_server.cpp writes it — keep this table's switch cases in
 * that same order if core::UnitType ever changes.
 */
#pragma once

#include <stdint.h>

namespace esp32
{
    enum class WireUnitType : uint8_t
    {
        None     = 0,
        Degrees  = 1,
        Percent  = 2,
        Pressure = 3,
        Rpm      = 4,
        Speed    = 5,
        Temp     = 6,
        Torque   = 7,
        Volume   = 8,
    };

    /** @returns The unit label to show alongside a signal's value, e.g.
     * "bar"/"psi" for Pressure depending on `is_metric`. */
    inline const char *unit_label(uint8_t unit_type, bool is_metric)
    {
        switch (static_cast<WireUnitType>(unit_type))
        {
            case WireUnitType::Degrees:
                return "\xC2\xB0"; /* degree sign, UTF-8 */
            case WireUnitType::Percent:
                return "%";
            case WireUnitType::Pressure:
                return is_metric ? "bar" : "psi";
            case WireUnitType::Rpm:
                return "rpm";
            case WireUnitType::Speed:
                return is_metric ? "kph" : "mph";
            case WireUnitType::Temp:
                return is_metric ? "\xC2\xB0"
                                   "C"
                                 : "\xC2\xB0"
                                   "F";
            case WireUnitType::Torque:
                return is_metric ? "Nm" : "lb-ft";
            case WireUnitType::Volume:
                return is_metric ? "L" : "gal";
            case WireUnitType::None:
            default:
                return "";
        }
    }

    /** Converts a metric value to imperial for the given unit type. No-op
     * for unit types with no imperial variant (None/Degrees/Percent/RPM),
     * matching unit_label() above having only one label for those. */
    inline float unit_convert_to_imperial(uint8_t unit_type, float metric_value)
    {
        switch (static_cast<WireUnitType>(unit_type))
        {
            case WireUnitType::Pressure:
                return metric_value * 14.5038f;
            case WireUnitType::Speed:
                return metric_value / 1.609f;
            case WireUnitType::Temp:
                return metric_value * 1.8f + 32.0f;
            case WireUnitType::Torque:
                return metric_value * 0.7375621493f;
            case WireUnitType::Volume:
                return metric_value / 3.785f;
            default:
                return metric_value;
        }
    }

    /** Inverse of unit_convert_to_imperial() — converts an imperial value
     * back to the signal's native (metric) storage/comparison unit. */
    inline float unit_convert_to_metric(uint8_t unit_type, float imperial_value)
    {
        switch (static_cast<WireUnitType>(unit_type))
        {
            case WireUnitType::Pressure:
                return imperial_value / 14.5038f;
            case WireUnitType::Speed:
                return imperial_value * 1.609f;
            case WireUnitType::Temp:
                return (imperial_value - 32.0f) / 1.8f;
            case WireUnitType::Torque:
                return imperial_value / 0.7375621493f;
            case WireUnitType::Volume:
                return imperial_value * 3.785f;
            default:
                return imperial_value;
        }
    }

    /** Mirrors gui::ACCENT_PRESETS (gui/theme/src/theme.cpp) — the only
     * accent_color values src/main.cpp's setup() accepts as valid on
     * boot; anything else gets silently reset to ACCENT_PRESETS[0] and
     * re-saved, so the phone form restricts its color choice to this
     * same list rather than offering a free color picker. */
    constexpr uint8_t  ACCENT_PRESET_COUNT                 = 9;
    constexpr uint32_t ACCENT_PRESETS[ACCENT_PRESET_COUNT] = {
        0x4CAF50, /* green (THEME_DEFAULT's default accent) */
        0x00BCD4, /* cyan */
        0x2196F3, /* blue */
        0x9C27B0, /* purple */
        0xE91E63, /* pink */
        0xF44336, /* red */
        0xFF9800, /* orange */
        0xFFC107, /* amber */
        0x9E9E9E, /* grayscale */
    };

} // namespace esp32
