/**
 * @file main_gauge.cpp
 * @brief Composition root for the analog gauge product: a 320x172 strip
 * showing three sensors read straight off an ADC, driven by two buttons.
 *
 * The second of two roots in this tree, selected by `build_src_filter`
 * exactly as `platform/<board>/src` is -- see platformio.ini. It carries no
 * preprocessor branches, and neither does src/main.cpp; a product
 * difference that needs an `#ifdef` belongs in platform/ or in a build
 * flag, never here.
 *
 * What it shares with the MQB root is most of the program: core/time,
 * core/vehicle, core/config, core/logging, all of gui/, and the four
 * subsystems under src/app/. What differs is only the three things this
 * product actually does differently:
 *
 *   1. **Acquisition.** core::AnalogSignalSource over IAnalogInput, in
 *      place of core::UdsSignalSource over ICanBus. Named twice, like the
 *      other one -- declaration and constructor -- because
 *      core::ISignalSource is the whole seam between them.
 *   2. **Input.** Two buttons through core::ButtonInput/core::ActionMap,
 *      driving app logic directly. There is no lv_indev on this product
 *      and no lv_group: buttons never navigate, which is the single
 *      largest simplification the touchless design buys.
 *   3. **Presentation.** One screen. gui::LayoutStyle::Strip, gui::
 *      THEME_GAUGE, a zoom cycle instead of eight screens of navigation,
 *      and a transient overlay for everything a press needs to acknowledge.
 *
 * **No settings UI, by design.** Every knob this product has is reachable
 * from the phone (Phase 7's web form over the ESP32-S3's on-die Wi-Fi).
 * The buttons exist for the handful of things worth doing at 70 mph
 * without looking, and nothing here is allowed to become a menu.
 */

#include "app/backlight.hpp"
#include "app/entry_point.hpp"
#include "app/log_console.hpp"
#include "app/lvgl_runtime.hpp"
#include "app/orientation.hpp"

#include "mem_placement.hpp"
#include "platform.hpp"
#include "theme.hpp"

#include "action_map.hpp"
#include "analog_signal_source.hpp"
#include "button_input.hpp"
#include "config_store.hpp"
#include "curve_library.hpp"
#include "device_config_store.hpp"
#include "logger.hpp"
#include "peak_hold.hpp"
#include "scheduler.hpp"
#include "signal_catalog.hpp"
#include "signal_source.hpp"
#include "signal_store.hpp"
#include "warning_latch.hpp"

#include "info_screen.hpp"
#include "toast_overlay.hpp"
#include "vehicle_binding.hpp"
#include "zoom_cycle.hpp"

#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>

namespace
{
    /**
     * ==================================================================
     * The board's wiring: which sensor is on which ADC channel, and
     * which catalog signal it feeds.
     *
     * This is the one thing a composition root is *supposed* to know --
     * the same way src/main.cpp names its CAN arbitration IDs. Nothing
     * below this file has any business knowing that oil pressure is on
     * channel 0.
     *=================================================================*/

    constexpr uint8_t CH_OIL_PRESSURE = 0;
    constexpr uint8_t CH_OIL_TEMP     = 1;
    constexpr uint8_t CH_COOLANT_TEMP = 2;
    constexpr uint8_t CH_RAIL_SENSE   = 3;
    constexpr uint8_t SENSOR_CHANNELS = 4;

    /* Ids from core/vehicle/data/gauge/signals.json. */
    constexpr uint16_t SIG_OIL_PRESSURE = 0;
    constexpr uint16_t SIG_OIL_TEMP     = 1;
    constexpr uint16_t SIG_COOLANT_TEMP = 2;

    /**
     * ==================================================================
     * Cadences
     *=================================================================*/

    /**
     * 20Hz, matching the MQB root -- 5Hz did not read as smooth on real
     * hardware, and nothing about a smaller panel changes that. */
    constexpr uint32_t INFO_SCREEN_REFRESH_MS = 50;

    /**
     * Faster than the display refresh on purpose. This is what advances
     * WarningLatch's qualification timer and folds new readings into
     * PeakHold, and both have to see every sample the source writes --
     * not every sample somebody happens to be looking at. Sampling the
     * whole catalog is three entries here. */
    constexpr uint32_t VEHICLE_STATE_PERIOD_MS = 25;

    constexpr uint32_t LOG_SAMPLE_PERIOD_MS = 25;

    /**
     * ==================================================================
     * The log ring
     *
     * core::Logger's defaults are a 13 MiB compile-time budget sized for
     * the CAN product's onboard flash, and it does not consult the
     * backend (recorded in TODO.md). This board's LittleFS partition is
     * 7.94 MB, so the default would be a ring the filesystem cannot hold.
     *
     * 4 MiB instead, and it buys *more* recording than the CAN product
     * gets rather than less: that ring carries six signals, this one
     * carries three, and core::Logger only writes on change.
     *=================================================================*/

    constexpr uint32_t LOG_CHUNK_BYTES = 128u * 1024u;
    constexpr uint16_t LOG_CHUNK_COUNT = 32; /* 4 MiB */

    /**
     * Backstop for re-pointing the source at the displayed signals --
     * every path that can change the set calls sync_active_signals()
     * directly, so this only catches a future one that forgets. */
    constexpr uint32_t ACTIVE_SIGNAL_SYNC_MS = 500;

    /**
     * ==================================================================
     * Warning policy
     *=================================================================*/

    /**
     * How long a threshold crossing must persist before it raises
     * anything (core::WarningLatch).
     *
     * This is the number that reconciles the build plan's two opposing
     * requirements: a brief excursion must not be missed (so it latches
     * until acknowledged), and a brief excursion must not alarm (because
     * with no RPM input there is no honest low-oil-pressure threshold --
     * see the plan's 9.1). A crossing shorter than this never happened
     * as far as the driver is concerned; anything longer is held until
     * somebody presses a button.
     */
    constexpr uint16_t WARNING_QUALIFY_MS = 1500;

    /**
     * Factory warning thresholds, in each signal's native metric unit.
     *
     * **Demonstration defaults, not a recommendation.** The low pressure
     * figure is deliberately below hot-idle pressure, which is the
     * conservative choice the build plan's 9.1 argues for in the absence
     * of an RPM input: it will not catch a genuine loss at 3000 rpm, and
     * it will not cry wolf at every stoplight. Tuning it is the owner's
     * job, from the phone, once Phase 7 lands -- which is also when these
     * move into the persisted config and this block goes away.
     */
    constexpr float OIL_PRESSURE_LO_BAR = 0.9f;  /* ~13 psi */
    constexpr float OIL_TEMP_HI_C       = 125.0f;
    constexpr float COOLANT_TEMP_HI_C   = 105.0f;

    /**
     * ==================================================================
     * Brightness
     *=================================================================*/

    /**
     * The ramp never reaches zero. No gesture may leave the screen
     * unreadable while driving -- that is what IDisplay's separate
     * set_backlight_off() is for, and it is deliberately not reachable
     * from a button.
     */
    constexpr uint8_t BRIGHTNESS_MIN_PCT  = 10;
    constexpr uint8_t BRIGHTNESS_MAX_PCT  = 100;
    constexpr uint8_t BRIGHTNESS_STEP_PCT = 10;

    constexpr uint32_t HINT_TOAST_MS = 4000;

    /**
     * ==================================================================
     * Composition
     *=================================================================*/

    struct Composition
    {
            core::ConfigStore config_store;
            /**
             * The hardware's own configuration -- what is wired to which
             * ADC channel, and what the buttons do. Its own blob at its
             * own offset, because it is ten times the size of the shared
             * display schema and describes nothing the CAN product has;
             * see device_config.hpp. */
            core::DeviceConfigStore device_config;
            core::SignalStore       signal_store;
            /**
             * The board's acquisition method, behind core::ISignalSource.
             * The CAN product's root declares core::UdsSignalSource here
             * and is otherwise identical. */
            core::AnalogSignalSource signal_source;
            core::Scheduler          scheduler;
            core::Logger             logger;

            core::PeakHold     peak_hold;
            core::WarningLatch warning_latch;

            core::ButtonInput buttons;
            core::ActionMap   actions;

            gui::VehicleBinding vehicle_binding;
            gui::InfoScreen     info_screen;
            gui::ToastOverlay   toast;
            gui::ZoomCycle      zoom;

            explicit Composition(Platform &platform)
                : config_store(platform.config_store)
                , device_config(platform.config_store)
                , signal_source(platform.analog, signal_store)
                , scheduler(platform.clock)
                , logger(platform.storage,
                         LOG_CHUNK_BYTES,
                         LOG_CHUNK_COUNT)
                , buttons(platform.buttons)
                , vehicle_binding(signal_store, config_store)
            {
            }
    };

    Composition &composition()
    {
        static Composition instance(platform_get());
        return instance;
    }

    /**
     * A mutable working copy of THEME_GAUGE, so a persisted accent color
     * has somewhere to be written. Same arrangement as the MQB root's,
     * minus the rebuild-every-screen machinery: this product has no theme
     * picker, so the accent only ever changes at boot.
     */
    gui::Theme &runtime_theme()
    {
        static gui::Theme instance = gui::THEME_GAUGE;
        return instance;
    }

    /** How many panels the overview shows: the product's whole catalog,
     * bounded by what a config blob can describe. Derived rather than
     * written as 3, so adding a fourth sensor is a registry edit. */
    uint8_t overview_panel_count()
    {
        uint8_t count = static_cast<uint8_t>(core::SIGNAL_CATALOG_COUNT);
        if (count > core::MAX_PANELS)
        {
            count = core::MAX_PANELS;
        }
        return count < 1 ? 1 : count;
    }

    /**
     * Every core::ISignalSource holds a full screen of panels' worth of
     * signals, which is exactly what core::Config::priorities[] can
     * describe -- asserted against the interface, so both roots' sources
     * inherit the guarantee. */
    static_assert(core::ISignalSource::MAX_ACTIVE_SIGNALS >= core::MAX_PANELS,
                  "a signal source cannot hold every displayable panel");

    /**
     * The two config blobs share one IConfigStore, and this is the only
     * file that sees both schemas -- so it is the only place that can
     * check they do not overlap. core::Config sits at offset 0; the
     * device blob starts at core::DEVICE_CONFIG_OFFSET. Overlapping them
     * would have each silently corrupt the other. */
    static_assert(sizeof(core::Config) <= core::DEVICE_CONFIG_OFFSET,
                  "core::Config has grown into the device config blob");

    void sync_active_signals()
    {
        Composition        &app = composition();
        const core::Config &cfg = app.config_store.get();

        uint8_t count = cfg.settings.num_panels;
        if (count > core::MAX_PANELS)
        {
            count = core::MAX_PANELS;
        }

        uint16_t ids[core::MAX_PANELS];
        for (uint8_t i = 0; i < count; i++)
        {
            ids[i] = cfg.priorities[i].signal_id;
        }

        app.signal_source.set_active(ids, count);
    }

    /**
     * ==================================================================
     * Sensor calibration
     *
     * Until Phase 7 adds core::SensorCal to the persisted config, these
     * are the factory numbers. Written as "apply the closest preset, then
     * correct the fields that describe the part actually fitted", which
     * is exactly the flow the web form will offer -- see
     * curve_library.hpp. Nothing downstream reads preset_id, so what the
     * conditioning maths sees is the corrected struct and only that.
     *=================================================================*/

    /* Preset ids from core/sensors/curve_library.cpp. */
    constexpr uint16_t PRESET_0V5_4V5_RATIOMETRIC = 1;
    constexpr uint16_t PRESET_GENERIC_NTC_2K2     = 11;
    constexpr uint16_t PRESET_RAIL_5V             = 20;

    /**
     * Full scale of the pressure transducer fitted, in bar (the native
     * metric unit for UnitType::Pressure). 150 psi, matching the part the
     * simulator models and the bench harness uses. The presets cannot
     * know this -- every pressure preset's note says to set it -- and
     * Phase 7's two-point capture writes it from a real measurement
     * rather than from a datasheet figure.
     */
    constexpr float PRESSURE_FS_BAR = 10.3421f; /* 150 psi */

    /**
     * The NTC senders' nominal resistance at 25 C. The closest built-in
     * preset is a 2.2k part; these are 2k, which is one corrected field
     * rather than a preset of their own -- and the reason the built-in
     * list does not need to contain every sender ever made.
     */
    constexpr float NTC_R25_OHMS = 2000.0f;

    uint8_t build_default_sensor_cals(core::SensorCal *out)
    {
        core::SensorCal &pressure = out[CH_OIL_PRESSURE];
        core::sensor_preset_apply(PRESET_0V5_4V5_RATIOMETRIC, pressure);
        pressure.channel           = CH_OIL_PRESSURE;
        pressure.signal_id         = SIG_OIL_PRESSURE;
        pressure.linear.value_hi   = PRESSURE_FS_BAR;

        core::SensorCal &oil = out[CH_OIL_TEMP];
        core::sensor_preset_apply(PRESET_GENERIC_NTC_2K2, oil);
        oil.channel                 = CH_OIL_TEMP;
        oil.signal_id               = SIG_OIL_TEMP;
        oil.thermistor.r25_ohms     = NTC_R25_OHMS;
        oil.thermistor.valid_max_c  = 150.0f;

        core::SensorCal &coolant = out[CH_COOLANT_TEMP];
        core::sensor_preset_apply(PRESET_GENERIC_NTC_2K2, coolant);
        coolant.channel                = CH_COOLANT_TEMP;
        coolant.signal_id              = SIG_COOLANT_TEMP;
        coolant.thermistor.r25_ohms    = NTC_R25_OHMS;
        /**
         * Lower than the oil channel's: a coolant sender that reads 140 C
         * is reporting something no cooling system does, and the band is
         * the only thing standing between that and a believable number. */
        coolant.thermistor.valid_max_c = 130.0f;

        core::SensorCal &rail = out[CH_RAIL_SENSE];
        core::sensor_preset_apply(PRESET_RAIL_5V, rail);
        rail.channel   = CH_RAIL_SENSE;
        rail.signal_id = core::SENSOR_SIGNAL_NONE; /* measured, never shown */

        return SENSOR_CHANNELS;
    }

    /**
     * Copies the live action map into the device blob.
     *
     * Deliberately not a method on either type. core::ActionMap should not
     * know it is ever persisted, and core::DeviceConfig is plain data --
     * so the eight lines that marshal between them belong to whoever owns
     * both, which is this file.
     */
    void store_action_map()
    {
        Composition        &app = composition();
        core::DeviceConfig &dc  = app.device_config.get();

        uint8_t count = app.actions.binding_count();
        if (count > core::ActionMap::MAX_BINDINGS)
        {
            count = core::ActionMap::MAX_BINDINGS;
        }

        for (uint8_t i = 0; i < count; i++)
        {
            dc.bindings[i] = app.actions.binding_at(i);
        }
        dc.header.binding_count = count;
    }

    /**
     * Installs the factory warning thresholds and persists them.
     *
     * Called only when the device blob was absent or rejected -- i.e.
     * exactly once, on a device that has never been configured. Until
     * 7.1 there was no way to know that, so these were re-applied on
     * every boot behind a "neither bound is enabled" guard; a threshold
     * the owner had deliberately switched *off* would have come back at
     * the next power cycle. `DeviceConfigStore::load()` returning false
     * is the honest first-boot signal, so the guard is gone.
     */
    void seed_default_warnings()
    {
        struct Seed
        {
                uint16_t id;
                bool     use_lo;
                float    lo;
                bool     use_hi;
                float    hi;
        };

        static const Seed SEEDS[] = {
            { SIG_OIL_PRESSURE, true, OIL_PRESSURE_LO_BAR, false, 0.0f },
            { SIG_OIL_TEMP, false, 0.0f, true, OIL_TEMP_HI_C },
            { SIG_COOLANT_TEMP, false, 0.0f, true, COOLANT_TEMP_HI_C },
        };

        core::Config &cfg = composition().config_store.get();

        for (const Seed &seed : SEEDS)
        {
            int idx = core::signal_catalog_find(seed.id);
            if (idx < 0)
            {
                continue;
            }

            core::WarningThresholds &warn = cfg.signals[idx].warn;
            warn.use_lo                   = seed.use_lo;
            warn.lo                       = seed.lo;
            warn.use_hi                   = seed.use_hi;
            warn.hi                       = seed.hi;
        }

        composition().config_store.save();
    }

    /**
     * Fills the device blob with this board's factory configuration and
     * writes it.
     *
     * Everything here is a fact about *this* product -- which sender is on
     * which channel, what the buttons do -- which is why it lives in the
     * composition root and not in core::DeviceConfigStore. Shared code
     * holds the container and the validity rules; the root holds the
     * numbers. (Phase 6.0's lesson, where six MQB catalog ids in
     * ConfigStore::set_defaults() were silently wrong for every other
     * product.)
     */
    void seed_device_config()
    {
        Composition        &app = composition();
        core::DeviceConfig &dc  = app.device_config.get();

        app.device_config.clear();

        dc.header.sensor_channels = build_default_sensor_cals(dc.sensors);

        app.actions.set_defaults(app.buttons.button_count());
        /**
         * One rebinding on top of the defaults: this board has no radio
         * until the ESP32-S3's on-die Wi-Fi lands, and a button that
         * silently does nothing is worse than one doing something useful.
         * It does have storage, so B-hold captures a drive instead.
         * Delete this when the radio arrives. */
        app.actions.bind(1,
                         core::ButtonEvent::LongPress,
                         core::InputAction::ToggleLogging);
        store_action_map();

        dc.warning_qualify_ms = WARNING_QUALIFY_MS;
        dc.sample_interval_ms
            = core::AnalogSignalSource::DEFAULT_SAMPLE_INTERVAL_MS;

        app.device_config.save();
    }

    /**
     * ==================================================================
     * Presentation
     *=================================================================*/

    gui::PanelViewModel view_model_builder(void   *ctx,
                                           uint8_t rank,
                                           bool    large,
                                           bool    hero)
    {
        auto *binding = static_cast<gui::VehicleBinding *>(ctx);
        return binding->build_view_model(rank, large, hero);
    }

    /** Pushes the zoom cycle's current position onto the screen. */
    void apply_zoom()
    {
        Composition &app = composition();

        app.info_screen.set_rank_offset(app.zoom.rank_offset());
        app.info_screen.rebuild(app.zoom.panel_count(),
                                gui::LayoutOrientation::Horizontal);
    }

    /** The rank a toast should talk about: whatever is zoomed, or nothing.
     * @returns The catalog index, or -1. */
    int zoomed_catalog_index()
    {
        Composition &app = composition();
        if (!app.zoom.zoomed())
        {
            return -1;
        }

        uint16_t id
            = app.config_store.get().priorities[app.zoom.zoomed_rank()].signal_id;
        return core::signal_catalog_find(id);
    }

    /**
     * Formats the extreme worth knowing for one signal.
     *
     * Which end that is depends on the signal and not on the reading: a
     * temperature's story is how hot it got, a pressure's is how low it
     * fell. PeakHold tracks both for everything precisely because the
     * choice is presentation.
     */
    void format_peak(int idx, bool is_metric, char *out, size_t cap)
    {
        Composition &app = composition();

        if (idx < 0 || !app.peak_hold.has_data_at(idx))
        {
            snprintf(out, cap, "no data yet");
            return;
        }

        const core::SignalSpec &spec = core::SIGNAL_CATALOG[idx];
        bool  want_min = spec.unit_type == core::UnitType::Pressure;
        float value    = want_min ? app.peak_hold.min_at(idx)
                                  : app.peak_hold.max_at(idx);

        if (!is_metric)
        {
            value = core::unit_convert_to_imperial(spec.unit_type, value);
        }

        snprintf(out,
                 cap,
                 "%s %.*f %s",
                 want_min ? "min" : "max",
                 core::unit_decimals(spec.unit_type, is_metric),
                 static_cast<double>(value),
                 core::unit_label(spec.unit_type, is_metric));
    }

    /** Short label for a gesture, for the hint overlay. The map's own
     * core::input_action_name() says what a binding *does*; this says how
     * to perform it, and the wording is a presentation choice rather than
     * something core/input should have an opinion about. */
    const char *gesture_label(core::ButtonEvent event)
    {
        switch (event)
        {
            case core::ButtonEvent::ShortPress:
                return "tap";
            case core::ButtonEvent::DoublePress:
                return "tap x2";
            case core::ButtonEvent::LongPress:
                return "hold";
            case core::ButtonEvent::VeryLongPress:
                return "hold 4s";
            case core::ButtonEvent::LongRelease:
                return "release";
            case core::ButtonEvent::HoldRepeat:
            case core::ButtonEvent::None:
            default:
                return "";
        }
    }

    const char *source_label(uint8_t source)
    {
        if (source == core::INPUT_SOURCE_CHORD)
        {
            return "A+B";
        }
        return source == 0 ? "A" : "B";
    }

    /**
     * Builds the gesture hint from the action map itself, so a remapped
     * binding cannot disagree with what the screen says it does.
     *
     * HoldRepeat and FactoryResetCommit are skipped: the first is the
     * continuation of a LongPress already listed, and the second is the
     * second half of a two-step the arm step announces on screen when it
     * happens. Listing either would spend lines on a 172px panel saying
     * something twice.
     */
    void show_gesture_hint()
    {
        Composition &app = composition();

        char     text[256];
        size_t   used = 0;
        text[0]       = '\0';

        for (uint8_t i = 0; i < app.actions.binding_count(); i++)
        {
            const core::ActionBinding &binding = app.actions.binding_at(i);

            if (binding.event == core::ButtonEvent::HoldRepeat
                || binding.action == core::InputAction::FactoryResetCommit)
            {
                continue;
            }

            int written = snprintf(text + used,
                                   sizeof(text) - used,
                                   "%s%-4s%-8s%s",
                                   used == 0 ? "" : "\n",
                                   source_label(binding.source),
                                   gesture_label(binding.event),
                                   core::input_action_name(binding.action));
            if (written <= 0
                || static_cast<size_t>(written) >= sizeof(text) - used)
            {
                break;
            }
            used += static_cast<size_t>(written);
        }

        app.toast.show(text, HINT_TOAST_MS);
    }

    /**
     * ==================================================================
     * Actions
     *=================================================================*/

    /** Which of Settings' two brightness levels is currently showing.
     * Runtime only -- the persisted pair is what the user configured, and
     * which one is in effect right now is a fact about the drive. */
    bool &night_active()
    {
        static bool night = false;
        return night;
    }

    uint8_t &active_brightness()
    {
        static uint8_t pct = BRIGHTNESS_MAX_PCT;
        return pct;
    }

    void apply_brightness(uint8_t pct, const char *prefix)
    {
        active_brightness() = pct;
        app::backlight::set_brightness(pct);

        char text[32];
        snprintf(text, sizeof(text), "%s %u%%", prefix, pct);
        composition().toast.show(text);
    }

    void action_cycle_zoom()
    {
        Composition &app = composition();

        app.zoom.advance();
        apply_zoom();

        int idx = zoomed_catalog_index();
        if (idx < 0)
        {
            app.toast.show("overview");
            return;
        }

        bool is_metric
            = app.config_store.get().priorities[app.zoom.zoomed_rank()].units
              == core::UnitSystem::Metric;

        /**
         * The zoom view is where somebody is looking closely at one
         * signal, which is exactly when its held extreme is worth
         * knowing -- and the only place PeakHold is visible without
         * spending permanent panel space on it. */
        char peak[40];
        format_peak(idx, is_metric, peak, sizeof(peak));

        char text[64];
        snprintf(text,
                 sizeof(text),
                 "%s  %s",
                 core::SIGNAL_CATALOG[idx].name,
                 peak);
        app.toast.show(text);
    }

    void action_toggle_day_night()
    {
        const core::Settings &settings
            = composition().config_store.get().settings;

        night_active() = !night_active();
        apply_brightness(night_active() ? settings.night_brightness
                                        : settings.day_brightness,
                         night_active() ? "night" : "day");
    }

    void action_ramp_brightness()
    {
        uint8_t pct = active_brightness();

        if (pct >= BRIGHTNESS_MAX_PCT)
        {
            pct = BRIGHTNESS_MIN_PCT;
        }
        else
        {
            uint16_t next = static_cast<uint16_t>(pct) + BRIGHTNESS_STEP_PCT;
            pct           = next > BRIGHTNESS_MAX_PCT
                                ? BRIGHTNESS_MAX_PCT
                                : static_cast<uint8_t>(next);
        }

        apply_brightness(pct, "brightness");
    }

    void action_reset_peak_hold()
    {
        Composition &app = composition();
        app.peak_hold.reset();
        app.toast.show("peaks cleared");
    }

    void action_toggle_units()
    {
        Composition  &app = composition();
        core::Config &cfg = app.config_store.get();

        /**
         * Every panel at once. Per-panel units exist in the schema and
         * the phone can set them, but a button that changed one panel's
         * units would need a notion of "the selected panel", which is the
         * first step towards navigation. */
        bool to_metric
            = cfg.priorities[0].units == core::UnitSystem::Imperial;

        for (uint8_t rank = 0; rank < core::MAX_PANELS; rank++)
        {
            cfg.priorities[rank].units = to_metric
                                             ? core::UnitSystem::Metric
                                             : core::UnitSystem::Imperial;
            app.config_store.save_priority(rank);
        }

        app.info_screen.refresh();
        app.toast.show(to_metric ? "metric" : "imperial");
    }

    void action_toggle_logging()
    {
        Composition &app = composition();

        bool enabled = !app.config_store.get().settings.logging_enabled;
        app.config_store.get().settings.logging_enabled = enabled;
        app.config_store.save();

        bool ready = enabled && app.logger.is_ready();
        app.info_screen.set_logging_active(ready);

        app.toast.show(!enabled          ? "logging off"
                       : ready           ? "logging on"
                                         : "no storage");
    }

    void action_acknowledge_warning()
    {
        Composition &app = composition();
        app.warning_latch.acknowledge();
        app.info_screen.refresh();
        app.toast.show("warning acknowledged");
    }

    bool &factory_reset_armed()
    {
        static bool armed = false;
        return armed;
    }

    void action_factory_reset_arm()
    {
        factory_reset_armed() = true;
        /**
         * Sticky (0 ms): this stays up for as long as the chord is held
         * and is taken down by whichever comes first, the commit or the
         * release. A countdown that dismissed itself would be a
         * confirmation prompt that expires while you are reading it. */
        composition().toast.show("keep holding both:\nfactory reset", 0);
    }

    void action_factory_reset_commit()
    {
        Composition &app = composition();

        factory_reset_armed() = false;

        /**
         * Invalidate the magic rather than reach for ConfigStore's own
         * private set_defaults(): load() already treats an unrecognised
         * header as "this is a fresh device", which is precisely what a
         * factory reset means, and it is the one path that is guaranteed
         * to stay correct as the schema grows. */
        core::ConfigHeader wiped { 0, 0 };
        platform_get().config_store.write(0, &wiped, sizeof(wiped));

        /**
         * Both blobs, or a "factory reset" leaves the calibration and the
         * gesture map exactly as they were -- which is the half a user
         * most likely wanted cleared, since a wrong sensor curve is what
         * sends somebody looking for a reset in the first place. */
        core::DeviceConfigHeader device_wiped {};
        platform_get().config_store.write(
            core::DEVICE_CONFIG_OFFSET, &device_wiped, sizeof(device_wiped));

        app.config_store.load();
        app.device_config.load();
        seed_device_config();
        seed_default_warnings();

        const core::DeviceConfig &dc = app.device_config.get();
        app.actions.load(dc.bindings, dc.header.binding_count);
        app.warning_latch.set_qualify_ms(dc.warning_qualify_ms);
        app.signal_source.set_sample_interval_ms(dc.sample_interval_ms);
        app.signal_source.configure(dc.sensors, dc.header.sensor_channels);

        app.peak_hold.reset();
        app.warning_latch.reset();
        app.zoom.reset();
        app.zoom.set_overview_count(
            app.config_store.get().settings.num_panels);

        sync_active_signals();
        apply_zoom();

        apply_brightness(app.config_store.get().settings.day_brightness,
                         "reset -- day");
    }

    void dispatch(core::InputAction action)
    {
        switch (action)
        {
            case core::InputAction::CycleZoom:
                action_cycle_zoom();
                break;
            case core::InputAction::ToggleDayNightBrightness:
                action_toggle_day_night();
                break;
            case core::InputAction::RampBrightness:
                action_ramp_brightness();
                break;
            case core::InputAction::ResetPeakHold:
                action_reset_peak_hold();
                break;
            case core::InputAction::ToggleUnits:
                action_toggle_units();
                break;
            case core::InputAction::ToggleLogging:
                action_toggle_logging();
                break;
            case core::InputAction::ShowGestureHint:
                show_gesture_hint();
                break;
            case core::InputAction::AcknowledgeWarning:
                action_acknowledge_warning();
                break;
            case core::InputAction::FactoryResetArm:
                action_factory_reset_arm();
                break;
            case core::InputAction::FactoryResetCommit:
                action_factory_reset_commit();
                break;

            /**
             * Mapped to nothing on this board. CycleZoomStart needs a
             * notion of "which signal the cycle begins on", which is a
             * phone setting rather than a gesture; ToggleWifiAp needs a
             * radio, which arrives with the ESP32-S3 in Phase 7. */
            case core::InputAction::CycleZoomStart:
            case core::InputAction::ToggleWifiAp:
            case core::InputAction::None:
            default:
                /**
                 * An unbound gesture is the one press that should explain
                 * itself. Cheaper than reserving a gesture for the hint,
                 * and it puts the reminder exactly where somebody has
                 * just demonstrated they need one. */
                show_gesture_hint();
                break;
        }
    }

    void poll_input(uint32_t now_ms)
    {
        Composition &app = composition();

        app.buttons.poll(now_ms);

        /**
         * Taken down here rather than in a handler: the chord ending
         * without a commit is a non-event, and there is no gesture for
         * "you let go". */
        if (factory_reset_armed() && !app.buttons.chord_active())
        {
            factory_reset_armed() = false;
            app.toast.hide();
        }

        bool latched = app.warning_latch.any_held();

        for (uint8_t i = 0; i < app.buttons.event_count(); i++)
        {
            const core::InputEvent &event = app.buttons.event_at(i);
            dispatch(
                app.actions.resolve(event.source, event.event, latched));
        }
    }

    /**
     * ==================================================================
     * Scheduled work
     *=================================================================*/

    void refresh_info_screen_task(void *ctx)
    {
        (void)ctx;
        composition().info_screen.refresh();
    }

    /**
     * Advances everything that has to see every reading rather than every
     * frame: peak/minimum hold, and warning qualification.
     *
     * Driven from here rather than from the view builder for one
     * load-bearing reason -- the zoom view draws one panel of three, and
     * a warning that only qualified while its panel happened to be on
     * screen would be a warning that depends on where you were looking.
     */
    void vehicle_state_task(void *ctx)
    {
        (void)ctx;

        Composition        &app = composition();
        const core::Config &cfg = app.config_store.get();
        uint32_t            now = platform_get().clock.millis();

        app.peak_hold.sample(app.signal_store);

        for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
        {
            app.warning_latch.update(static_cast<int>(i),
                                     app.signal_store.get_at(
                                         static_cast<int>(i)),
                                     cfg.signals[i].warn,
                                     now);
        }
    }

    void sync_active_signals_task(void *ctx)
    {
        (void)ctx;
        sync_active_signals();
    }

    void logging_sample_task(void *ctx)
    {
        (void)ctx;
        Composition &app = composition();
        app.logger.sample(app.signal_store,
                          app.config_store.get(),
                          platform_get().clock.millis());
    }
} // namespace

FLASHMEM void setup()
{
    app::log_console::begin();

    Platform &platform = platform_get();

    Composition    &app      = composition();
    core::Settings &settings = app.config_store.get().settings;
    app.config_store.load();

    /**
     * Landscape, always. A 320x172 strip has no portrait orientation --
     * it is a fact about the panel, not a preference, so
     * settings.orientation is deliberately not consulted and the
     * SettingsScreen that would have edited it is not compiled in. */
    constexpr DisplayOrientation ORIENTATION = DisplayOrientation::Landscape;

    platform.display.begin();
    platform.display.set_orientation(ORIENTATION);
    platform.buttons.begin();
    platform.analog.begin();
    platform.storage.begin();
    app.logger.begin();

    uint16_t width;
    uint16_t height;
    platform.display.get_resolution(width, height);

    /**
     * No pointer input device. There is no touchscreen behind it, and an
     * lv_indev polling a no-op ITouch forever is the one thing a
     * touchless product must not create. */
    app::lvgl_runtime::begin({ width, height, /* with_pointer_indev */ false });

    bool valid_accent = false;
    for (uint8_t i = 0; i < gui::ACCENT_PRESET_COUNT; i++)
    {
        if (settings.accent_color == gui::ACCENT_PRESETS[i])
        {
            valid_accent = true;
            break;
        }
    }
    if (!valid_accent)
    {
        settings.accent_color = gui::ACCENT_PRESETS[0];
        app.config_store.save();
    }
    runtime_theme().palette.accent = lv_color_hex(settings.accent_color);

    /**
     * Three signals, three panels, and no way on this product to ask for
     * a different number -- the phone can reorder them, not add a fourth
     * that does not exist. Clamped rather than trusted, since the config
     * blob is shared with a product that has 22 signals. */
    if (settings.num_panels > overview_panel_count())
    {
        settings.num_panels = overview_panel_count();
        app.config_store.save();
    }

    /**
     * The calibration and the action map, or a first boot. Everything
     * this product can be configured with now survives a power cycle;
     * before 7.1 the calibrations were compiled-in constants and the
     * gesture map was rebuilt from defaults every time. */
    if (!app.device_config.load())
    {
        seed_device_config();
        seed_default_warnings();
    }

    const core::DeviceConfig &dc = app.device_config.get();

    app.actions.load(dc.bindings, dc.header.binding_count);
    app.warning_latch.set_qualify_ms(dc.warning_qualify_ms);
    app.signal_source.set_sample_interval_ms(dc.sample_interval_ms);
    app.signal_source.configure(dc.sensors, dc.header.sensor_channels);

    /**
     * The panels read their warning highlight from the latch rather than
     * evaluating thresholds live, so a qualified warning stays lit after
     * the value has come back inside. */
    app.vehicle_binding.set_warning_latch(&app.warning_latch);

    app.info_screen.init(runtime_theme(),
                         view_model_builder,
                         &app.vehicle_binding,
                         /* press_cb */ nullptr,
                         nullptr);
    /**
     * Equal full-height bands. At 172px tall no panel can afford to give
     * up vertical space to a neighbour, which is what Grid's hero-plus-
     * stack would make two of them do. */
    app.info_screen.set_layout_style(gui::LayoutStyle::Strip);
    app.info_screen.set_logging_active(settings.logging_enabled
                                       && app.logger.is_ready());

    app.toast.init(runtime_theme());

    app.zoom.set_overview_count(settings.num_panels);
    apply_zoom();

    /**
     * A fault, not a limitation, so it gets said out loud. A board whose
     * IConfigStore is too small looks exactly like one booting for the
     * first time -- every time -- and silently accepts changes it will
     * never keep. Bringing up a new board is precisely when this
     * happens. */
    if (app.device_config.storage_too_small())
    {
        app.toast.show("config storage too small\nsettings will not persist", 0);
    }

    sync_active_signals();
    app.scheduler.every(
        ACTIVE_SIGNAL_SYNC_MS, sync_active_signals_task, nullptr);
    app.scheduler.every(
        INFO_SCREEN_REFRESH_MS, refresh_info_screen_task, nullptr);
    app.scheduler.every(
        VEHICLE_STATE_PERIOD_MS, vehicle_state_task, nullptr);
    app.scheduler.every(LOG_SAMPLE_PERIOD_MS, logging_sample_task, nullptr);

    app::backlight::arm();
    active_brightness() = settings.day_brightness;
    app::backlight::set_brightness(settings.day_brightness);

    lv_screen_load(app.info_screen.screen());
}

void loop()
{
    Composition &app = composition();
    uint32_t     now = platform_get().clock.millis();

    app::log_console::poll(app.logger);

    app.scheduler.tick();
    app.signal_source.tick(now);
    poll_input(now);

    lv_timer_handler();
}
