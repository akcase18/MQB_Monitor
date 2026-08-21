/**
 * @file main.cpp
 * @brief Composition root. Builds the target board's Platform, the core::
 * domain stack (Scheduler driving UDS polling, SignalStore, ConfigStore),
 * and the gui:: presentation stack (VehicleBinding + the four screens) on
 * top of it. This is the only file that references a concrete
 * platform_get() implementation, and the only place that depends on both
 * core:: and gui:: directly outside of gui/bindings.
 */

#include "platform.hpp"
#include "theme.hpp"

#include "config_store.hpp"
#include "logger.hpp"
#include "scheduler.hpp"
#include "signal_catalog.hpp"
#include "signal_store.hpp"
#include "uds_client.hpp"

#include "remote_ap_controller.hpp"
#include "remote_config_link.hpp"
#include "remote_config_server.hpp"

#include "configurator_screen.hpp"
#include "info_screen.hpp"
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

#ifdef ARDUINO
    #include <Arduino.h>
#endif

namespace
{
    constexpr uint16_t LCD_HORZ = 480;
    constexpr uint16_t LCD_VERT = 320;

    /* Standard OBD2 enhanced-diagnostic-session request/response arbitration
     * IDs, ported from the old can_handler.cpp. Real-vehicle validation is a
     * Phase 7 hardware-flash-test concern. */
    constexpr uint32_t CAN_TX_ID = 0x7E0;
    constexpr uint32_t CAN_RX_ID = 0x7E8;
    constexpr uint32_t CAN_BAUD  = 500000;

    /* Teensy<->ESP32-C3 wireless-configuration link. See remote/protocol
     * for the framing this carries. */
    constexpr uint32_t REMOTE_SERIAL_BAUD = 115200;

    constexpr uint32_t INFO_SCREEN_REFRESH_MS     = 200;
    constexpr uint32_t WIRELESS_SCREEN_REFRESH_MS = 500;

    /* How often the logger *looks*, not how often it writes -- the write
     * rate per signal is floored at core::LOG_MIN_INTERVAL_MS (40ms)
     * inside Logger::sample() itself. Matching the catalog's fastest
     * cadence (20ms, see signal_catalog.generated.inc) means a 20ms
     * signal is seen on the tick it actually arrives, so that floor
     * produces clean 40ms spacing rather than the ragged 40/60ms a
     * coarser tick would give once scheduler jitter is in play. Slower
     * signals (100ms and up) are unaffected either way -- they're logged
     * at their own arrival cadence. Sampling twice as often costs
     * essentially nothing: each tick walks at most MAX_PANELS entries. */
    constexpr uint32_t LOG_SAMPLE_PERIOD_MS = 20;

    lv_color_t s_draw_buf_1[(LCD_HORZ * LCD_VERT) / 10];
    lv_color_t s_draw_buf_2[(LCD_HORZ * LCD_VERT) / 10];

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
            core::UdsClient   uds_client;
            core::Scheduler   scheduler;
            core::Logger      logger;

            remote::server::RemoteConfigServer remote_server;
            remote::server::RemoteConfigLink   remote_link;
            remote::server::RemoteApController remote_ap;

            gui::VehicleBinding vehicle_binding;
            gui::RemoteBinding  remote_binding;

            gui::InfoScreen          info_screen;
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
                , uds_client(platform.can, CAN_TX_ID, CAN_RX_ID)
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

    gui::LayoutOrientation to_gui_orientation(
        core::LayoutOrientation orientation)
    {
        return orientation == core::LayoutOrientation::Vertical
                   ? gui::LayoutOrientation::Vertical
                   : gui::LayoutOrientation::Horizontal;
    }

    core::LayoutOrientation to_core_orientation(
        gui::LayoutOrientation orientation)
    {
        return orientation == gui::LayoutOrientation::Vertical
                   ? core::LayoutOrientation::Vertical
                   : core::LayoutOrientation::Horizontal;
    }

    DisplayOrientation to_display_orientation(
        gui::LayoutOrientation orientation)
    {
        return orientation == gui::LayoutOrientation::Vertical
                   ? DisplayOrientation::Portrait
                   : DisplayOrientation::Landscape;
    }

    void display_flush_cb(lv_display_t    *disp,
                          const lv_area_t *area,
                          uint8_t         *px_map)
    {
        platform_get().display.flush(
            area->x1,
            area->y1,
            area->x2,
            area->y2,
            reinterpret_cast<const uint16_t *>(px_map));
        lv_display_flush_ready(disp);

        /* The draw buffer only covers a fraction of the screen (see
         * s_draw_buf_1's size below), so the panel's first real frame
         * takes several flush() calls to land in full — turning the
         * backlight on after just the first of those would show the rest
         * of the screen still holding whatever garbage was in GRAM from
         * power-on. lv_display_flush_is_last() marks the flush that
         * completes the current render pass, so wait for that instead.
         * TeensyDisplay::begin() leaves the backlight pin held low for
         * exactly this reason (see its own comment). */
        static bool s_first_frame_flushed = false;
        if (!s_first_frame_flushed && lv_display_flush_is_last(disp))
        {
            s_first_frame_flushed = true;
            platform_get().display.set_backlight(100);
        }
    }

    void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
    {
        (void)indev;

        /* Also pumps the simulator's SDL event queue (window close, etc). */
        TouchPoint point {};
        bool       pressed = platform_get().touch.read(point);

        data->point.x = point.x;
        data->point.y = point.y;
        data->state
            = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }

    uint32_t tick_cb()
    {
        return platform_get().clock.millis();
    }

    /** Fires every SIGNAL_CATALOG entry's own cycle_time_ms, sending all of
     * that signal's UDS requests. `ctx` is the catalog index, encoded
     * directly as a pointer value (no heap, no context struct needed). */
    void send_signal_requests_task(void *ctx)
    {
        uint16_t idx = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(ctx));
        const core::SignalSpec &spec = core::SIGNAL_CATALOG[idx];

        for (uint8_t r = 0; r < spec.num_requests; r++)
        {
            composition().uds_client.send(spec.requests[r]);
        }
    }

    void refresh_info_screen_task(void *ctx)
    {
        (void)ctx;
        composition().info_screen.refresh();
    }

    /* Single shared task rather than one per signal (unlike
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

    void on_uds_response(void *ctx, uint8_t byte1, uint8_t byte2, float raw)
    {
        (void)ctx;
        composition().signal_store.on_uds_response(
            byte1, byte2, raw, platform_get().clock.millis());
    }

    /* Request/response pairs are handled internally by
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

    /* Registered once at remote::server::AP_IDLE_CHECK_PERIOD_MS via
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

    gui::DisplayMode to_gui_display_mode(core::DisplayMode mode)
    {
        switch (mode)
        {
            case core::DisplayMode::Bar:
                return gui::DisplayMode::Bar;
            case core::DisplayMode::Gauge:
                return gui::DisplayMode::Gauge;
            case core::DisplayMode::Digital:
            default:
                return gui::DisplayMode::Digital;
        }
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
            to_gui_display_mode(panel.display_mode));
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

    /* Fired immediately on every brightness slider move / Day-Night toggle
     * — pushes straight to the real backlight so switching (or tuning
     * either value) is actually useful mid-drive, not just a setting
     * that takes effect after leaving Settings. */
    void settings_preview_brightness_cb(void *ctx, uint8_t brightness_pct)
    {
        (void)ctx;
        platform_get().display.set_backlight(brightness_pct);
    }

    void settings_done_cb(void   *ctx,
                          uint8_t day_brightness_pct,
                          uint8_t night_brightness_pct,
                          uint8_t num_panels)
    {
        (void)ctx;
        /* No set_backlight() here — settings_preview_brightness_cb already
         * pushed the real backlight live on every slider move and Day/
         * Night toggle, so it's already showing whichever of day/night was
         * actually active when Done was pressed. Re-applying
         * day_brightness_pct unconditionally here was wrong: it stomped
         * over a Night-mode edit with the (usually untouched, 100%) Day
         * value on every exit. */

        core::Settings &settings = composition().config_store.get().settings;

        /* Orientation isn't touched here — it's persisted immediately by
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

        composition().info_screen.rebuild(
            num_panels, to_gui_orientation(active_orientation()));
        lv_screen_load(composition().info_screen.screen());
    }

    /* Fired the moment Confirm is pressed on SettingsScreen's orientation
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
        settings.orientation     = to_core_orientation(orientation);
        composition().config_store.save();
    }

#ifdef ARDUINO
    /* USB-serial log retrieval -- a small bespoke line command set (not
     * SLCAN; that's can_sniffer.cpp's separate, unrelated firmware/
     * protocol for a different Teensy board entirely), modeled on the same
     * "line assembly, dispatch on CR/LF" shape can_sniffer.cpp already
     * uses for its own USB-CDC command parsing. Deliberately blocking --
     * this is meant to run as its own dedicated offload action (park,
     * plug in, dump), not interleaved with driving, so freezing
     * everything else (CAN polling, GUI) for the duration is fine. */
    void handle_log_command(const char *cmd, size_t len)
    {
        if (len == 7 && memcmp(cmd, "LOGDUMP", 7) == 0)
        {
            IStorage &storage = platform_get().storage;
            for (uint16_t i = 0; i < core::CHUNK_COUNT; i++)
            {
                char path[core::CHUNK_PATH_LEN];
                core::chunk_path(i, path);
                if (!storage.open(path, StorageFileMode::Read))
                {
                    continue; /* never written -- nothing to stream */
                }

                uint8_t buf[256];
                size_t  n;
                while ((n = storage.read(buf, sizeof(buf))) > 0)
                {
                    Serial.write(buf, n);
                }
                storage.close();
            }
        }
        else if (len == 8 && memcmp(cmd, "LOGCLEAR", 8) == 0)
        {
            /* Exactly what IStorage::format()'s own doc comment describes
             * this for -- reclaim space once logs are offloaded. */
            platform_get().storage.format();
        }
        else if (len == 9 && memcmp(cmd, "LOGSTATUS", 9) == 0)
        {
            core::Logger &logger = composition().logger;
            Serial.print("ready=");
            Serial.print(logger.is_ready() ? 1 : 0);
            Serial.print(" session=");
            Serial.print(logger.session_id());
            Serial.print(" chunk=");
            Serial.println(logger.current_chunk_index());
        }
    }

    void poll_log_serial()
    {
        static char   cmd[16];
        static size_t cmd_len = 0;

        while (Serial.available() > 0)
        {
            char c = static_cast<char>(Serial.read());
            if (c == '\r' || c == '\n')
            {
                if (cmd_len > 0)
                {
                    handle_log_command(cmd, cmd_len);
                    cmd_len = 0;
                }
            }
            else if (cmd_len < sizeof(cmd) - 1)
            {
                cmd[cmd_len++] = c;
            }
            else
            {
                cmd_len = 0; /* overflow -- drop the line */
            }
        }
    }
#endif

    void rebuild_screens_for_theme_change(void *user_data);

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

        lv_async_call(rebuild_screens_for_theme_change, nullptr);
    }

    /* Opening this screen activates the AP immediately — no separate
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

    void wireless_deactivate_cb(void *ctx)
    {
        (void)ctx;
        composition().remote_binding.deactivate();
    }

    /* Registered at a modest cadence (see WIRELESS_SCREEN_REFRESH_MS) so
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

        if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_BOTTOM)
        {
            composition().settings_screen.open();
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
    void build_screens()
    {
        Composition          &app      = composition();
        const gui::Theme     &theme    = runtime_theme();
        const core::Settings &settings = app.config_store.get().settings;

        app.info_screen.init(theme,
                             view_model_builder,
                             &app.vehicle_binding,
                             panel_pressed_cb,
                             nullptr);
        app.info_screen.set_logging_active(app.logger.is_ready());
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
                                 to_gui_orientation(settings.orientation),
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
        app.theme_picker_screen.init(theme, theme_picker_done_cb, nullptr);
        app.wireless_setup_screen.init(theme, wireless_deactivate_cb, nullptr);

        lv_obj_add_event_cb(app.info_screen.screen(),
                            info_screen_gesture_cb,
                            LV_EVENT_GESTURE,
                            nullptr);

        /* active_orientation(), not settings.orientation -- this runs
         * again from rebuild_screens_for_theme_change() mid-session, by
         * which point settings.orientation may already hold a staged
         * pick that hasn't taken effect yet (see settings_done_cb()). */
        app.info_screen.rebuild(settings.num_panels,
                                to_gui_orientation(active_orientation()));
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
    lv_obj_t *destroy_screens()
    {
        Composition &app = composition();

        lv_obj_t *transition = lv_obj_create(nullptr);
        lv_screen_load(transition);

        lv_obj_delete(app.info_screen.screen());
        lv_obj_delete(app.configurator_screen.screen());
        lv_obj_delete(app.signal_picker_screen.screen());
        lv_obj_delete(app.warning_screen.screen());
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
    void rebuild_screens_for_theme_change(void *user_data)
    {
        (void)user_data;
        lv_obj_t *transition = destroy_screens();
        build_screens();
        composition().settings_screen.open();
        lv_obj_delete(transition);
    }
} // namespace

void setup()
{
#ifdef ARDUINO
    Serial.begin(115200);
#endif

    Platform &platform = platform_get();

    /* Loaded before display/touch begin() so settings.orientation is
     * known up front -- orientation is a boot-time-only choice (see
     * platform.hpp's DisplayOrientation doc comments), and the one
     * lv_display_t created below has to be sized correctly from the
     * start since nothing resizes it live. */
    Composition     &app      = composition();
    core::Settings  &settings = app.config_store.get().settings;
    app.config_store.load();

    active_orientation() = settings.orientation;

    DisplayOrientation orientation
        = to_display_orientation(to_gui_orientation(settings.orientation));

    platform.display.begin();
    platform.display.set_orientation(orientation);
    platform.touch.begin();
    platform.touch.set_orientation(orientation);
    platform.can.begin(CAN_TX_ID, CAN_RX_ID, CAN_BAUD);
    platform.serial.begin(REMOTE_SERIAL_BAUD);
    platform.storage.begin();
    app.logger.begin();

    lv_init();
    lv_tick_set_cb(tick_cb);

    uint16_t width;
    uint16_t height;
    platform.display.get_resolution(width, height);

    lv_display_t *display = lv_display_create(width, height);
    lv_display_set_flush_cb(display, display_flush_cb);
    lv_display_set_buffers(display,
                           s_draw_buf_1,
                           s_draw_buf_2,
                           sizeof(s_draw_buf_1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    /* Guard against an accent_color that isn't one of gui::ACCENT_PRESETS
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

    /* Seed runtime_theme()'s accent and metrics from whatever was last
     * saved, before the first build_screens() call — otherwise every
     * screen would briefly be built against THEME_DEFAULT's hardcoded
     * green/landscape-metrics instead of the user's actual saved
     * preference. */
    runtime_theme().palette.accent = lv_color_hex(settings.accent_color);
    runtime_theme().metrics = orientation == DisplayOrientation::Portrait
                                  ? gui::METRICS_PORTRAIT
                                  : gui::THEME_DEFAULT.metrics;
    build_screens();

    for (uint16_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
    {
        app.scheduler.every(
            core::SIGNAL_CATALOG[i].cycle_time_ms,
            send_signal_requests_task,
            reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    }
    app.scheduler.every(
        INFO_SCREEN_REFRESH_MS, refresh_info_screen_task, nullptr);
    app.scheduler.every(remote::server::AP_IDLE_CHECK_PERIOD_MS,
                        remote_ap_idle_check_task,
                        nullptr);
    app.scheduler.every(
        WIRELESS_SCREEN_REFRESH_MS, refresh_wireless_screen_task, nullptr);
    app.scheduler.every(LOG_SAMPLE_PERIOD_MS, logging_sample_task, nullptr);

    lv_screen_load(app.info_screen.screen());
}

void loop()
{
    Composition &app = composition();

#ifdef ARDUINO
    poll_log_serial();
#endif

    app.scheduler.tick();
    app.uds_client.poll(on_uds_response, nullptr);
    app.remote_link.poll(remote_evt_cb, nullptr, platform_get().clock.millis());

    lv_timer_handler();
}

#if !defined(ARDUINO) && !defined(UNIT_TEST)
int main()
{
    setup();

    for (;;)
    {
        loop();
    }

    return 0;
}
#endif
