/**
 * @file main.cpp
 * @brief Composition root. Builds the target board's Platform, the core::
 * domain stack (a signal source feeding SignalStore, plus Scheduler and
 * ConfigStore), and the gui:: presentation stack (VehicleBinding + the
 * screens) on top of it. This is the only file that references a concrete
 * platform_get() implementation, and the only place that depends on both
 * core:: and gui:: directly outside of gui/bindings.
 *
 * Acquisition reaches this file only through core::ISignalSource --
 * set_active() to say which signals are wanted, tick() once per loop pass.
 * The concrete core::UdsSignalSource is named exactly twice, in its
 * declaration and its constructor call; nothing else here knows a CAN bus
 * exists. Building this product against analog sensors instead is a change
 * to those two places.
 */

#include "app/backlight.hpp"
#include "app/orientation.hpp"
#include "app/entry_point.hpp"
#include "app/log_console.hpp"
#include "app/lvgl_runtime.hpp"

#include "mem_placement.hpp"
#include "platform.hpp"
#include "theme.hpp"

#include "config_store.hpp"
#include "logger.hpp"
#include "scheduler.hpp"
#include "signal_catalog.hpp"
#include "signal_source.hpp"
#include "signal_store.hpp"
#include "uds_signal_source.hpp"

#include "remote_ap_controller.hpp"
#include "remote_config_link.hpp"
#include "remote_config_server.hpp"

#include "configurator_screen.hpp"
#include "info_screen.hpp"
#include "logging_screen.hpp"
#include "remote_binding.hpp"
#include "settings_screen.hpp"
#include "signal_picker_screen.hpp"
#include "theme_picker_screen.hpp"
#include "vehicle_binding.hpp"
#include "warning_config_screen.hpp"
#include "wireless_setup_screen.hpp"

#include <lvgl.h>
#include <stdint.h>
#include <string.h>

namespace
{
    /**
     * Standard OBD2 enhanced-diagnostic-session request/response arbitration
     * IDs, ported from the old can_handler.cpp. Real-vehicle validation is a
     * Phase 7 hardware-flash-test concern. */
    constexpr uint32_t CAN_TX_ID = 0x7E0;
    constexpr uint32_t CAN_RX_ID = 0x7E8;
    constexpr uint32_t CAN_BAUD  = 500000;

    /**
     * Teensy<->ESP32-C3 wireless-configuration link. See remote/protocol
     * for the framing this carries. */
    constexpr uint32_t REMOTE_SERIAL_BAUD = 115200;

    /**
     * 20Hz. Was 200ms (5Hz) from the Phase 6 rework until 2026-08-25 --
     * an architectural default carried over when the old "update the
     * screen every loop iteration" model became a scheduled task, not a
     * measured choice. 5Hz did not read as smooth on real hardware. The
     * pre-rework code invalidated every panel label on every loop pass
     * and let LVGL coalesce it to its own 20ms tick, so 50Hz redraws are
     * known to have run on this panel; 20Hz is well inside that. */
    constexpr uint32_t INFO_SCREEN_REFRESH_MS = 50;

    /**
     * Backstop cadence for re-pointing core::SignalPoller at the displayed
     * panel set. Every path that can change that set calls
     * sync_active_signals() directly, so this only ever catches a future
     * one that forgets to -- half a second is imperceptible for a change
     * the user just made by hand, and an unchanged list costs a six-entry
     * comparison. */
    constexpr uint32_t ACTIVE_SIGNAL_SYNC_MS = 500;

    constexpr uint32_t WIRELESS_SCREEN_REFRESH_MS = 500;
    /**
     * Slower than the info screen's: nothing here changes faster than
     * once a minute except the capture toggle, which repaints itself on
     * press rather than waiting for this. */
    constexpr uint32_t LOGGING_SCREEN_REFRESH_MS = 1000;

    /**
     * How often the logger *looks*, not how often it writes -- the write
     * rate per signal is floored at core::LOG_MIN_INTERVAL_MS (50ms)
     * inside Logger::sample() itself. Matching the catalog's fastest
     * cadence (20ms, see signal_catalog.generated.inc) means a 20ms
     * signal is seen on the tick it actually arrives, so that floor
     * produces clean 40ms spacing rather than the ragged 40/60ms a
     * coarser tick would give once scheduler jitter is in play. Slower
     * signals (100ms and up) are unaffected either way -- they're logged
     * at their own arrival cadence. Sampling twice as often costs
     * essentially nothing: each tick walks at most MAX_PANELS entries. */
    /**
     * Half core::LOG_MIN_INTERVAL_MS, preserving the relationship the
     * original 20/40 pair had: the tick has to be finer than the floor,
     * or a signal arriving exactly on the floor gets skipped whenever
     * jitter puts its arrival a hair inside the window and the next tick
     * a hair outside. Was 20 while the floor was 40. */
    constexpr uint32_t LOG_SAMPLE_PERIOD_MS = 25;

    /**
     * Every long-lived object the app needs, wired together once. Lazily
     * constructed on first access (Meyer's singleton, matching the pattern
     * platform's HAL classes already use) so it can safely depend on
     * platform_get() without any static-initialization-order concerns, and
     * so setup()/loop()/the LVGL callbacks below can all reach the same
     * instance without heap allocation.
     */
    struct Composition
    {
            core::ConfigStore config_store;
            core::SignalStore signal_store;
            /**
             * The board's acquisition method, behind core::ISignalSource.
             * Swapping CAN for analog sensors is a change to this one
             * declaration and its constructor arguments. */
            core::UdsSignalSource signal_source;
            core::Scheduler       scheduler;
            core::Logger          logger;

            remote::server::RemoteConfigServer remote_server;
            remote::server::RemoteConfigLink   remote_link;
            remote::server::RemoteApController remote_ap;

            gui::VehicleBinding vehicle_binding;
            gui::RemoteBinding  remote_binding;

            gui::InfoScreen          info_screen;
            gui::LoggingScreen       logging_screen;
            gui::ConfiguratorScreen  configurator_screen;
            gui::SignalPickerScreen  signal_picker_screen;
            gui::WarningConfigScreen warning_screen;
            gui::SettingsScreen      settings_screen;
            gui::ThemePickerScreen   theme_picker_screen;
            gui::WirelessSetupScreen wireless_setup_screen;

            const char *signal_names[core::SIGNAL_CATALOG_COUNT];
            uint16_t    signal_ids[core::SIGNAL_CATALOG_COUNT];

            explicit Composition(Platform &platform)
                : config_store(platform.config_store)
                , signal_source(platform.can,
                                platform.clock,
                                signal_store,
                                CAN_TX_ID,
                                CAN_RX_ID)
                , scheduler(platform.clock)
                , logger(platform.storage)
                , remote_server(config_store, signal_store)
                , remote_link(platform.serial, remote_server)
                , remote_ap(remote_link, platform.clock)
                , vehicle_binding(signal_store, config_store)
                , remote_binding(remote_ap)
            {
                for (uint16_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
                {
                    signal_names[i] = core::SIGNAL_CATALOG[i].name;
                    signal_ids[i]   = core::SIGNAL_CATALOG[i].id;
                }
            }
    };

    Composition &composition()
    {
        static Composition instance(platform_get());
        return instance;
    }

    /**
     * A mutable working copy of THEME_DEFAULT — every screen is built
     * against this, not the const global directly, so a user-picked
     * accent color (SettingsScreen's Theme row) has something to write
     * into. Screens don't re-read this live; picking a new color rebuilds
     * every screen against the updated copy (see rebuild_screens_for_
     * theme_change()) rather than hunting down every already-applied
     * accent-colored style.
     */
    gui::Theme &runtime_theme()
    {
        static gui::Theme instance = gui::THEME_DEFAULT;
        return instance;
    }

    /**
     * The panel-grid orientation actually in effect on the physical
     * display right now -- set once in setup() from whatever
     * settings.orientation held at boot, and never touched again for the
     * life of the process. settings.orientation, by contrast, gets
     * overwritten the moment Done is pressed after a toggle (see
     * settings_done_cb()) even though the physical panel hasn't rotated
     * yet (orientation is boot-time-only, see platform.hpp's
     * DisplayOrientation) -- anywhere InfoScreen gets rebuilt live must
     * read this, not settings.orientation, or a second toggle in the
     * same session would reflow against tomorrow's orientation on
     * today's still-unrotated canvas.
     */
    core::LayoutOrientation &active_orientation()
    {
        static core::LayoutOrientation instance
            = core::LayoutOrientation::Horizontal;
        return instance;
    }


    /**
     * Every core::ISignalSource holds a full screen of panels' worth of
     * signals, which is exactly what core::Config::priorities[] can
     * describe. If one grows past the other, sync_active_signals() below
     * would silently stop acquiring the panels past the cut. Asserted
     * against the interface rather than one implementation, so a future
     * source inherits the guarantee; this file is the only one allowed to
     * see both core/vehicle and core/config. */
    static_assert(core::ISignalSource::MAX_ACTIVE_SIGNALS >= core::MAX_PANELS,
                  "a signal source cannot hold every displayable panel");

    /** Points the poller at the signals the dashboard is currently showing
     * -- `priorities[0..num_panels-1]`, which is the complete set of live
     * values anything reads (gui::VehicleBinding::build_view_model() for
     * the panels, core::Logger::sample() for the log, and nothing else).
     * Every other catalog entry is dead weight on the bus until the user
     * puts it on a panel.
     *
     * Cheap and idempotent: core::SignalPoller::set_active() returns
     * immediately when the list has not changed, so this is called both
     * from the config-change callbacks (for immediacy) and from a periodic
     * backstop task (so a future config path that forgets to call it still
     * cannot leave the poller asking for the wrong signals). */
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

    void sync_active_signals_task(void *ctx)
    {
        (void)ctx;
        sync_active_signals();
    }

    void refresh_info_screen_task(void *ctx)
    {
        (void)ctx;
        composition().info_screen.refresh();
    }

    /**
     * Single shared task rather than one per signal (unlike
     * send_signal_requests_task above) -- core::Scheduler::MAX_TASKS is a
     * fixed 32-slot table already carrying 22 signal-poll tasks plus a
     * handful more, so a second one-per-signal registration isn't an
     * option even before considering that Logger::sample() already walks
     * the displayed set itself each call. */
    void logging_sample_task(void *ctx)
    {
        (void)ctx;
        Composition &app = composition();
        app.logger.sample(app.signal_store,
                          app.config_store.get(),
                          platform_get().clock.millis());
    }

    /**
     * Request/response pairs are handled internally by
     * RemoteConfigLink::poll(); this only ever sees the async EVT_*
     * messages (EVT_AP_READY/EVT_AP_DISABLED). */
    FLASHMEM void remote_evt_cb(void                     *ctx,
                                remote::protocol::MsgType type,
                                const uint8_t            *payload,
                                uint16_t                  len)
    {
        (void)ctx;
        composition().remote_ap.on_event(
            type, payload, len, platform_get().clock.millis());
    }

    /**
     * Registered once at remote::server::AP_IDLE_CHECK_PERIOD_MS via
     * core::Scheduler — a no-op unless RemoteApController actually has a
     * timeout to act on (see remote::server::RemoteApController::tick()). */
    FLASHMEM void remote_ap_idle_check_task(void *ctx)
    {
        (void)ctx;
        composition().remote_ap.tick(platform_get().clock.millis());
    }

    gui::PanelViewModel view_model_builder(void   *ctx,
                                           uint8_t rank,
                                           bool    large,
                                           bool    hero)
    {
        auto *binding = static_cast<gui::VehicleBinding *>(ctx);
        return binding->build_view_model(rank, large, hero);
    }

    void panel_pressed_cb(void *ctx, uint8_t rank)
    {
        (void)ctx;
        const core::Config            &cfg   = composition().config_store.get();
        const core::PrioritizedSignal &panel = cfg.priorities[rank];

        composition().configurator_screen.open(
            rank,
            panel.signal_id,
            panel.units == core::UnitSystem::Metric,
            gui::to_gui_display_mode(panel.display_mode));
    }

    void configurator_done_cb(void            *ctx,
                              uint8_t          rank,
                              uint16_t         signal_id,
                              bool             is_metric,
                              gui::DisplayMode mode)
    {
        (void)ctx;
        composition().vehicle_binding.apply_configurator_change(
            rank,
            signal_id,
            is_metric ? core::UnitSystem::Metric : core::UnitSystem::Imperial,
            mode);
        sync_active_signals();
        composition().info_screen.refresh();
        lv_screen_load(composition().info_screen.screen());
    }

    void configurator_settings_cb(void *ctx, uint8_t rank, uint16_t signal_id)
    {
        (void)ctx;

        const core::Config            &cfg   = composition().config_store.get();
        const core::PrioritizedSignal &panel = cfg.priorities[rank];
        bool is_metric = panel.units == core::UnitSystem::Metric;

        gui::WarningThresholdViewModel vm
            = composition().vehicle_binding.read_warning_thresholds(signal_id,
                                                                    is_metric);
        composition().warning_screen.open(
            signal_id, vm, composition().configurator_screen.screen());
    }

    void configurator_pick_signal_cb(void *ctx, uint16_t current_signal_id)
    {
        (void)ctx;
        composition().signal_picker_screen.open(
            current_signal_id, composition().configurator_screen.screen());
    }

    void signal_picker_done_cb(void *ctx, uint16_t signal_id)
    {
        (void)ctx;
        composition().configurator_screen.set_selected_signal(signal_id);
    }

    void warning_done_cb(void                                 *ctx,
                         uint16_t                              signal_id,
                         const gui::WarningThresholdViewModel &result)
    {
        (void)ctx;
        composition().vehicle_binding.apply_warning_change(signal_id, result);
    }

    /**
     * Fired immediately on every brightness slider move / Day-Night toggle
     * — pushes straight to the real backlight so switching (or tuning
     * either value) is actually useful mid-drive, not just a setting
     * that takes effect after leaving Settings. */
    void settings_preview_brightness_cb(void *ctx, uint8_t brightness_pct)
    {
        (void)ctx;
        app::backlight::set_brightness(brightness_pct);
    }

    void settings_done_cb(void   *ctx,
                          uint8_t day_brightness_pct,
                          uint8_t night_brightness_pct,
                          uint8_t num_panels)
    {
        (void)ctx;
        /**
         * No set_backlight() here — settings_preview_brightness_cb already
         * pushed the real backlight live on every slider move and Day/
         * Night toggle, so it's already showing whichever of day/night was
         * actually active when Done was pressed. Re-applying
         * day_brightness_pct unconditionally here was wrong: it stomped
         * over a Night-mode edit with the (usually untouched, 100%) Day
         * value on every exit. */

        core::Settings &settings = composition().config_store.get().settings;

        /**
         * Orientation isn't touched here — it's persisted immediately by
         * settings_orientation_confirm_cb() the moment Confirm is pressed
         * on the popup, not deferred to Done (see SettingsScreen's file
         * doc comment for why: the popup itself already reads as the
         * commit step). InfoScreen keeps reflowing against
         * active_orientation() (set once at boot, never touched again)
         * rather than settings.orientation, since orientation is a boot-
         * time-only setting (see platform.hpp's DisplayOrientation/
         * set_orientation() doc comments) -- the physical panel hasn't
         * rotated just because Done was pressed. */
        settings.day_brightness   = day_brightness_pct;
        settings.night_brightness = night_brightness_pct;
        settings.num_panels       = num_panels;
        composition().config_store.save();
        sync_active_signals();

        composition().info_screen.rebuild(
            num_panels, app::to_gui_orientation(active_orientation()));
        lv_screen_load(composition().info_screen.screen());
    }

    /**
     * Fired the moment Confirm is pressed on SettingsScreen's orientation
     * popup — persisted right away rather than waiting for Settings'
     * own Done, so a power-cycle before Done is ever pressed doesn't lose
     * the change the popup just told the user was committed. Mirrors
     * theme_picker_done_cb()'s same immediate-persist pattern for accent
     * color. Still boot-time-only to actually apply — see
     * settings_done_cb()'s comment on active_orientation(). */
    void settings_orientation_confirm_cb(void                  *ctx,
                                         gui::LayoutOrientation orientation)
    {
        (void)ctx;
        core::Settings &settings = composition().config_store.get().settings;
        settings.orientation     = app::to_core_orientation(orientation);
        composition().config_store.save();
    }

    /** Snapshot of everything LoggingScreen shows, assembled from the
     * Logger and the active IStorage. Rebuilt per open/refresh rather
     * than cached -- all of it is in-RAM reads (see Logger's status
     * accessors -- and IStorage::medium(), a compile-time constant). */
    gui::LoggingViewModel build_logging_view_model()
    {
        Composition &app     = composition();
        IStorage    &storage = platform_get().storage;

        gui::LoggingViewModel vm;
        vm.recording     = app.config_store.get().settings.logging_enabled;
        vm.storage_ready = app.logger.is_ready();
        vm.log_count     = app.logger.log_count();
        vm.total_logged_min
            = app.logger.total_logged_ms() / 60000u; /**
                                                      * whole minutes,
                                                      * no hour rollover
                                                      * -- see the view
                                                      * model */
        vm.ring_capacity_bytes = app.logger.ring_capacity_bytes();
        vm.used_bytes          = app.logger.used_bytes();

        /**
         * The one place platform::StorageMedium is translated for gui/,
         * which has no platform:: dependency of its own. */
        const char *label = "Unknown";
        switch (storage.medium())
        {
            case StorageMedium::Flash:
                label = "Flash";
                break;
            case StorageMedium::SdCard:
                label = "SD Card";
                break;
            case StorageMedium::HostDirectory:
                label = "Simulated";
                break;
        }
        strncpy(vm.storage_label, label, gui::LOGGING_STORAGE_LABEL_LEN - 1);
        vm.storage_label[gui::LOGGING_STORAGE_LABEL_LEN - 1] = '\0';

        return vm;
    }

    /**
     * Fired the moment LoggingScreen's toggle is tapped. Persisted right
     * away (same immediate-commit pattern as
     * settings_orientation_confirm_cb/theme_picker_done_cb) and pushed
     * straight to InfoScreen's status glyph, which is where the user sees
     * whether capture is running once this screen is closed.
     * core::Logger itself needs no notification -- it reads
     * Settings::logging_enabled live on its next sample() tick, which is
     * also what opens/closes the capture in its log index. */
    void logging_toggle_cb(void *ctx, bool enabled)
    {
        (void)ctx;
        Composition &app = composition();

        app.config_store.get().settings.logging_enabled = enabled;
        app.config_store.save();
        app.info_screen.set_logging_active(enabled && app.logger.is_ready());
    }

    /**
     * Fired once LoggingScreen's erase popup is confirmed -- the GUI
     * counterpart of the LOGCLEAR serial command below, and the same one
     * call.
     *
     * Recording is stopped first (which is what the popup's "Stops
     * recording" says): leaving it on would have sample() open a fresh
     * capture into the store moments after wiping it, so the count the
     * user just took to zero would start climbing again on its own.
     * Logger::erase() drops the in-RAM tail of the stopped capture too,
     * so nothing buffered before the wipe survives into the first chunk
     * written after it.
     *
     * The screen is refreshed here rather than left to
     * refresh_logging_screen_task's next tick -- this is a button press,
     * and the readouts should be zero by the time the popup is gone. */
    void logging_erase_cb(void *ctx)
    {
        (void)ctx;
        Composition &app = composition();

        if (app.config_store.get().settings.logging_enabled)
        {
            app.config_store.get().settings.logging_enabled = false;
            app.config_store.save();
            app.info_screen.set_logging_active(false);
        }

        app.logger.erase();
        app.logging_screen.refresh(build_logging_view_model());
    }

    /**
     * Keeps the capture count and recorded-minutes readouts advancing
     * while the screen is up. Cheap enough to run unconditionally: like
     * refresh_wireless_screen_task, it only does real work when its
     * screen is actually loaded. */
    void refresh_logging_screen_task(void *ctx)
    {
        (void)ctx;
        Composition &app = composition();
        if (lv_screen_active() == app.logging_screen.screen())
        {
            app.logging_screen.refresh(build_logging_view_model());
        }
    }


    void rebuild_screens_for_theme_change(void *user_data);
    void settings_screen_loaded_cb(lv_event_t *e);

    void settings_pick_theme_cb(void *ctx)
    {
        (void)ctx;
        Composition &app = composition();
        app.theme_picker_screen.open(
            app.config_store.get().settings.accent_color,
            app.settings_screen.screen());
    }

    void theme_picker_done_cb(void *ctx, uint32_t accent_color)
    {
        (void)ctx;
        runtime_theme().palette.accent = lv_color_hex(accent_color);

        core::Settings &settings = composition().config_store.get().settings;
        settings.accent_color    = accent_color;
        composition().config_store.save();

        /**
         * Every screen is about to be deleted and rebuilt, which is not
         * something the panel should be showing: the blank transition
         * screen destroy_screens() loads reads as a white flash, and
         * SettingsScreen then slides back in from the top. Killed here
         * rather than inside the rebuild so the panel is already dark
         * before the swatch's own press-release even finishes drawing.
         * settings_screen_loaded_cb brings it back. */
        app::backlight::blank_for_transition();

        lv_async_call(rebuild_screens_for_theme_change, nullptr);
    }

    /**
     * The other half of theme_picker_done_cb's blank_backlight(). Fires
     * on the rebuilt SettingsScreen once it has finished loading --
     * including the 220ms slide-down animation SettingsScreen::open()
     * starts, since LVGL sends this from the load animation's own
     * completion (lv_display.c's scr_anim_completed), not when the load
     * is merely requested. That's the end of the *animation*; the panel
     * then waits for the display to go quiet, the same settle the boot
     * path takes after its first frame.
     *
     * Registered once per screen build and left in place: it's a no-op
     * unless the display was deliberately blanked, so ordinary trips
     * into Settings pass straight through it. */
    void settings_screen_loaded_cb(lv_event_t *e)
    {
        (void)e;
        /**
         * Guarded inside, so a second SCREEN_LOADED -- the event fires
         * on every trip into Settings -- can't restart the wait for a
         * panel that is already lit. */
        app::backlight::begin_settle_if_blanked();
    }

    /**
     * Opening this screen activates the AP immediately — no separate
     * Activate step (see wireless_setup_screen.hpp's file doc comment).
     * activate() is a no-op unless the AP is currently Idle, so reopening
     * while already Active/Activating just shows the current state. */
    void settings_pick_wireless_cb(void *ctx)
    {
        (void)ctx;
        Composition &app = composition();
        app.remote_binding.activate();
        app.wireless_setup_screen.open(app.remote_binding.build_view_model(),
                                       app.settings_screen.screen());
    }

    /** Set when a phone-side Save commits, cleared when the resulting
     * rebuild is kicked off. Deferred rather than acted on immediately
     * because the apply lands while WirelessSetupScreen is on-screen, and
     * rebuilding there would delete the screen the user is looking at
     * mid-session. Coalescing also means several Saves in one sitting cost
     * one rebuild, not one each. */
    bool &remote_config_dirty()
    {
        static bool dirty = false;
        return dirty;
    }

    /**
     * Fired by RemoteConfigServer once a phone-side Save has validated,
     * committed and persisted. The config store has already changed; every
     * screen is still built against the values from the last
     * build_screens(), so they have to be rebuilt before any of this shows.
     *
     * Brightness is the one thing pushed live here rather than deferred,
     * since the local path (settings_preview_brightness_cb) applies it on
     * every slider move and a remote change that did nothing until the
     * screen closed would read as broken. Day is assumed: which of
     * day/night is "current" lives in SettingsScreen::showing_night_ and
     * isn't visible from here, and Day is what SettingsScreen opens in.
     * Editing night brightness from the phone therefore stores correctly
     * but won't be what the panel shows until night is selected locally.
     *
     * Orientation is NOT applied live and cannot be -- it is boot-time-only
     * (see platform.hpp's DisplayOrientation). The rebuilt SettingsScreen
     * shows the newly saved value; the panel rotates on the next restart,
     * exactly as it does for a local orientation change. */
    void remote_config_applied_cb(void *ctx)
    {
        (void)ctx;
        app::backlight::set_brightness(
            composition().config_store.get().settings.day_brightness);

        /**
         * A remote apply can rewrite priorities[] and num_panels wholesale,
         * so the poller's request table has to follow it. */
        sync_active_signals();
        remote_config_dirty() = true;
    }

    void wireless_deactivate_cb(void *ctx)
    {
        (void)ctx;
        composition().remote_binding.deactivate();

        if (!remote_config_dirty())
        {
            return;
        }
        remote_config_dirty() = false;

        /**
         * Same treatment as an accent change: the rebuild flashes a blank
         * transition screen, so kill the backlight first and let
         * settings_screen_loaded_cb bring it back once the rebuilt
         * SettingsScreen has finished its slide-in. */
        app::backlight::blank_for_transition();
        lv_async_call(rebuild_screens_for_theme_change, nullptr);
    }

    /**
     * Registered at a modest cadence (see WIRELESS_SCREEN_REFRESH_MS) so
     * the Activating -> Active transition (an async round trip to the C3)
     * and an idle-timeout auto-deactivation both show up while this
     * screen is on-screen — unconditional like refresh_info_screen_task,
     * since WirelessSetupScreen::refresh() is a cheap no-op unless the
     * phase actually changed. */
    void refresh_wireless_screen_task(void *ctx)
    {
        (void)ctx;
        composition().wireless_setup_screen.refresh(
            composition().remote_binding.build_view_model());
    }

    void info_screen_gesture_cb(lv_event_t *e)
    {
        if (lv_event_get_code(e) != LV_EVENT_GESTURE)
        {
            return;
        }

        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

        if (dir == LV_DIR_BOTTOM)
        {
            composition().settings_screen.open();
        }
        else if (dir == LV_DIR_TOP)
        {
            Composition &app = composition();
            app.logging_screen.open(build_logging_view_model(),
                                    app.info_screen.screen());
        }
    }

    /**
     * Creates every screen against runtime_theme()'s current values and
     * builds the Info screen's panel grid. Called once from setup(), and
     * again (after destroy_screens()) whenever the accent color changes —
     * baked-in per-widget styles (button-matrix checked color, slider
     * indicator, etc.) don't re-read the Theme live, so picking up a new
     * accent means every screen has to be rebuilt from scratch against it.
     */
    FLASHMEM void build_screens()
    {
        Composition          &app      = composition();
        const gui::Theme     &theme    = runtime_theme();
        const core::Settings &settings = app.config_store.get().settings;

        app.info_screen.init(theme,
                             view_model_builder,
                             &app.vehicle_binding,
                             panel_pressed_cb,
                             nullptr);
        app.info_screen.set_logging_active(settings.logging_enabled
                                           && app.logger.is_ready());
        app.configurator_screen.init(theme,
                                     app.signal_names,
                                     app.signal_ids,
                                     core::SIGNAL_CATALOG_COUNT,
                                     configurator_done_cb,
                                     nullptr,
                                     configurator_settings_cb,
                                     nullptr,
                                     configurator_pick_signal_cb,
                                     nullptr);
        app.signal_picker_screen.init(theme,
                                      app.signal_names,
                                      app.signal_ids,
                                      core::SIGNAL_CATALOG_COUNT,
                                      signal_picker_done_cb,
                                      nullptr);
        app.warning_screen.init(theme, warning_done_cb, nullptr);
        app.settings_screen.init(theme,
                                 settings.day_brightness,
                                 settings.night_brightness,
                                 settings.num_panels,
                                 settings.accent_color,
                                 app::to_gui_orientation(settings.orientation),
                                 settings_done_cb,
                                 nullptr,
                                 settings_preview_brightness_cb,
                                 nullptr,
                                 settings_pick_theme_cb,
                                 nullptr,
                                 settings_pick_wireless_cb,
                                 nullptr,
                                 settings_orientation_confirm_cb,
                                 nullptr);
        app.logging_screen.init(
            theme, logging_toggle_cb, nullptr, logging_erase_cb, nullptr);
        app.theme_picker_screen.init(theme, theme_picker_done_cb, nullptr);
        app.wireless_setup_screen.init(theme, wireless_deactivate_cb, nullptr);

        lv_obj_add_event_cb(app.info_screen.screen(),
                            info_screen_gesture_cb,
                            LV_EVENT_GESTURE,
                            nullptr);
        lv_obj_add_event_cb(app.settings_screen.screen(),
                            settings_screen_loaded_cb,
                            LV_EVENT_SCREEN_LOADED,
                            nullptr);

        /**
         * active_orientation(), not settings.orientation -- this runs
         * again from rebuild_screens_for_theme_change() mid-session, by
         * which point settings.orientation may already hold a staged
         * pick that hasn't taken effect yet (see settings_done_cb()). */
        app.info_screen.rebuild(settings.num_panels,
                                app::to_gui_orientation(active_orientation()));
    }

    /**
     * Tears down every screen built by build_screens(), returning a
     * transient blank screen the caller must lv_obj_delete() once a real
     * screen has been loaded in its place. Switches to that blank screen
     * before deleting anything — LVGL doesn't clear a display's "active
     * screen" pointer just because that object gets deleted out from under
     * it, so deleting the currently-loaded screen directly (without
     * switching away first) would leave that pointer dangling.
     */
    FLASHMEM lv_obj_t *destroy_screens()
    {
        Composition &app = composition();

        lv_obj_t *transition = lv_obj_create(nullptr);
        lv_screen_load(transition);

        lv_obj_delete(app.info_screen.screen());
        lv_obj_delete(app.configurator_screen.screen());
        lv_obj_delete(app.signal_picker_screen.screen());
        lv_obj_delete(app.warning_screen.screen());
        lv_obj_delete(app.logging_screen.screen());
        lv_obj_delete(app.settings_screen.screen());
        lv_obj_delete(app.theme_picker_screen.screen());
        lv_obj_delete(app.wireless_setup_screen.screen());

        return transition;
    }

    /**
     * Runs via lv_async_call() rather than directly from theme_picker_
     * done_cb() — that callback fires from inside the very swatch button's
     * own LV_EVENT_CLICKED handler, and destroy_screens() deletes that
     * button (along with everything else) as part of tearing down
     * ThemePickerScreen; deleting a widget from inside its own event
     * callback is exactly the reentrancy LVGL's async-call mechanism
     * exists to avoid. By the time this runs, that call stack has fully
     * unwound.
     */
    FLASHMEM void rebuild_screens_for_theme_change(void *user_data)
    {
        (void)user_data;

        /**
         * Re-seed from the saved config rather than assuming the caller
         * already set it. theme_picker_done_cb does set runtime_theme()'s
         * accent before calling here, but the remote-apply path changes
         * settings.accent_color in the config store without going anywhere
         * near that function -- reading it back here covers both. */
        runtime_theme().palette.accent = lv_color_hex(
            composition().config_store.get().settings.accent_color);

        lv_obj_t *transition = destroy_screens();
        build_screens();
        composition().settings_screen.open();
        lv_obj_delete(transition);
    }
} // namespace

FLASHMEM void setup()
{
    app::log_console::begin();

    Platform &platform = platform_get();

    /**
     * Loaded before display/touch begin() so settings.orientation is
     * known up front -- orientation is a boot-time-only choice (see
     * platform.hpp's DisplayOrientation doc comments), and the one
     * lv_display_t created below has to be sized correctly from the
     * start since nothing resizes it live. */
    Composition    &app      = composition();
    core::Settings &settings = app.config_store.get().settings;
    app.config_store.load();

    active_orientation() = settings.orientation;

    DisplayOrientation orientation = app::to_display_orientation(
        app::to_gui_orientation(settings.orientation));

    platform.display.begin();
    platform.display.set_orientation(orientation);
    platform.touch.begin();
    platform.touch.set_orientation(orientation);
    platform.can.begin(CAN_TX_ID, CAN_RX_ID, CAN_BAUD);
    platform.serial.begin(REMOTE_SERIAL_BAUD);
    platform.storage.begin();
    app.logger.begin();

    uint16_t width;
    uint16_t height;
    platform.display.get_resolution(width, height);

    app::lvgl_runtime::begin({ width, height, /* with_pointer_indev */ true });

    /**
     * Guard against an accent_color that isn't one of gui::ACCENT_PRESETS
     * (e.g. 0 from a config saved before this field existed) — silently
     * building every screen against black-on-black accent styling is a
     * much worse failure mode than just falling back to a real default,
     * and this also self-heals the saved value so it doesn't recur. */
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

    /**
     * Seed runtime_theme()'s accent and metrics from whatever was last
     * saved, before the first build_screens() call — otherwise every
     * screen would briefly be built against THEME_DEFAULT's hardcoded
     * green/landscape-metrics instead of the user's actual saved
     * preference. */
    runtime_theme().palette.accent = lv_color_hex(settings.accent_color);
    runtime_theme().metrics        = orientation == DisplayOrientation::Portrait
                                         ? gui::METRICS_PORTRAIT
                                         : gui::THEME_DEFAULT.metrics;

    app.remote_server.set_applied_callback(remote_config_applied_cb, nullptr);

    build_screens();

    /**
     * No per-signal scheduler task any more -- core::SignalPoller paces
     * itself from loop() off its own due times and its outstanding
     * request, which a fixed-period task cannot express. This only has to
     * tell it what to ask for. */
    sync_active_signals();
    app.scheduler.every(
        ACTIVE_SIGNAL_SYNC_MS, sync_active_signals_task, nullptr);

    app.scheduler.every(
        INFO_SCREEN_REFRESH_MS, refresh_info_screen_task, nullptr);
    app.scheduler.every(remote::server::AP_IDLE_CHECK_PERIOD_MS,
                        remote_ap_idle_check_task,
                        nullptr);
    app.scheduler.every(
        WIRELESS_SCREEN_REFRESH_MS, refresh_wireless_screen_task, nullptr);
    app.scheduler.every(LOG_SAMPLE_PERIOD_MS, logging_sample_task, nullptr);
    app.scheduler.every(
        LOGGING_SCREEN_REFRESH_MS, refresh_logging_screen_task, nullptr);

    /**
     * Arms the backstop for the boot blank. The panel is already dark
     * (TeensyDisplay::begin() holds the pin low) and display_flush_cb's
     * pass counting is what normally lights it -- this is purely so a
     * boot where those passes never arrive still ends with a lit
     * dashboard rather than a black one. Has to come after lv_init(),
     * since it creates an lv_timer. */
    app::backlight::arm();

    lv_screen_load(app.info_screen.screen());
}

void loop()
{
    Composition &app = composition();

    app::log_console::poll(app.logger);

    app.scheduler.tick();

    app.signal_source.tick(platform_get().clock.millis());

    app.remote_link.poll(remote_evt_cb, nullptr, platform_get().clock.millis());

    lv_timer_handler();
}
