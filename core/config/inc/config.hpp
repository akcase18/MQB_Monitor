/**
 * @file config.hpp
 * @brief Persisted configuration schema, stored over IConfigStore. Warning
 * thresholds are keyed by signal (Config.signals[], each record tagged with
 * its signal id, and indexed by catalog position once loaded) rather than
 * by panel slot, so moving a signal to a different panel carries its
 * thresholds with it automatically. Config.priorities[]
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

    /**
     * Which level the backlight shows. Persisted, and its ordinal goes
     * over the wire to the phone configurator: append only.
     */
    enum class BrightnessMode : uint8_t
    {
        Day,   /* Settings::day_brightness */
        Night, /* Settings::night_brightness */
        Auto   /* follows the car's dimmer -- core::AutoBrightness */
    };

    /**
     * Settings::auto_brightness_pct's default and midpoint: the car's
     * dimmer level shown as it is.
     */
    constexpr uint8_t AUTO_BRIGHTNESS_NEUTRAL = 50;

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
            /* v8. Sits in what was padding before orientation, so the
             * layout didn't move -- but that padding was never guaranteed
             * zero, so v8 still needed a version bump and a migrate()
             * step that sets it. */
            BrightnessMode brightness_mode;
            LayoutOrientation orientation;
            /* Whether core::Logger records anything. Opt-in: logging is
             * a deliberate "capture this drive" action, not something
             * that should burn flash write-cycles on every commute, so
             * this defaults off and is toggled from SettingsScreen. */
            bool logging_enabled;
            /* Whether each layout is turned a further 180 degrees -- the
             * panel mounted the other way up. One per layout, since
             * which way is down in one says nothing about the other.
             * Picked on OrientationScreen for whichever layout is active,
             * applied live (unlike orientation, which needs a restart).
             * These and the flag below sit in what was padding before
             * accent_color, so a config saved before they existed reads
             * them as false and every other field stays put -- no
             * version bump needed. */
            bool flipped_horizontal;
            bool flipped_vertical;
            /* One-shot, not a preference: set just before a layout
             * change restarts the board, so the next boot lands back on
             * OrientationScreen instead of the dashboard, then cleared by
             * that boot. */
            bool open_orientation_on_boot;
            /* Packed 0xRRGGBB — one of gui::ACCENT_PRESETS, picked via
             * SettingsScreen's Theme row. Stored as a raw value rather than
             * an lv_color_t so core:: stays LVGL-free; gui/bindings
             * converts it at the boundary. */
            uint32_t accent_color;
            /* v9. Auto's brightness slider, 0-100: 50 shows the car's
             * dimmer level as it is, either end shifts it by up to
             * AutoBrightness::MAX_SHIFT_PCT. After accent_color, so the
             * v6-v8 Settings is a prefix of this one. */
            uint8_t auto_brightness_pct;
    };

    /** A single priority-ranked signal assignment — see the file doc
     * comment on Config.priorities[] for what the index means. */
    struct PrioritizedSignal
    {
            uint16_t    signal_id;
            UnitSystem  units;
            DisplayMode display_mode = DisplayMode::Digital;
    };

    /**
     * SignalConfig::signal_id of a slot that holds no signal's settings.
     * Also what erased flash reads as.
     */
    constexpr uint16_t NO_SIGNAL_ID = 0xFFFF;

    /**
     * How many SignalConfig records the stored config has room for. Fixed,
     * so promoting a signal or reordering the registry never changes the
     * stored layout: ConfigStore::load() matches records to signals by id.
     * Room beyond the active catalog keeps the settings of signals this
     * build no longer has, so demoting a signal and promoting it again
     * gets its thresholds back. 64 x 28 bytes keeps Config under 2 KB of
     * RAM and well inside the smallest IConfigStore (4 KB). Raising it
     * changes the layout, so it needs a CURRENT_VERSION bump.
     */
    constexpr uint8_t SIGNAL_CONFIG_SLOTS = 64;

    static_assert(SIGNAL_CATALOG_COUNT <= SIGNAL_CONFIG_SLOTS,
                  "more active signals than Config::signals[] has room for "
                  "-- raise SIGNAL_CONFIG_SLOTS and migrate the old layout");

    struct SignalConfig
    {
            WarningThresholds warn;
            /* Per-vehicle override of SignalSpec::display_min/max — resolve
             * through core::resolve_display_range() rather than reading
             * these directly. */
            bool  use_custom_range = false;
            float custom_min       = 0.0f;
            float custom_max       = 0.0f;
            /**
             * Which signal these settings belong to, stored with them so a
             * load can find them wherever they sit. Last, so the v6
             * record (everything above) is a prefix of this one.
             */
            uint16_t signal_id = NO_SIGNAL_ID;
    };

    struct Config
    {
            ConfigHeader      header;
            Settings          settings;
            PrioritizedSignal priorities[MAX_PANELS];
            /**
             * After ConfigStore::load(), signals[i] belongs to
             * SIGNAL_CATALOG[i] for every i below SIGNAL_CATALOG_COUNT, so
             * callers index it by catalog position as before. The slots
             * past that hold signals this build doesn't have, or nothing.
             */
            SignalConfig signals[SIGNAL_CONFIG_SLOTS];
    };

} // namespace core
