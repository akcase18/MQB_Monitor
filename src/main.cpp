/**
 * @file main.cpp
 * @brief Composition root. Builds the target board's Platform, the core::
 * domain stack (Scheduler driving UDS polling, SignalStore, ConfigStore),
 * and the gui:: presentation stack (VehicleBinding + the four screens) on
 * top of it. This is the only file that references a concrete
 * platform_get() implementation, and the only place that depends on both
 * core:: and gui:: directly outside of gui/bindings.
 */

#include "mem_placement.hpp"
#include "platform.hpp"
#include "theme.hpp"

#include "config_store.hpp"
#include "logger.hpp"
#include "scheduler.hpp"
#include "signal_catalog.hpp"
#include "signal_poller.hpp"
#include "signal_store.hpp"
#include "slcan.hpp"
#include "uds_client.hpp"

#include "remote_ap_controller.hpp"
#include "remote_config_link.hpp"
#include "remote_config_server.hpp"

#include "configurator_screen.hpp"
#include "info_screen.hpp"
#include "logging_screen.hpp"
#include "remote_binding.hpp"
#include "screen_helpers.hpp"
#include "settings_screen.hpp"
#include "signal_picker_screen.hpp"
#include "sniffer_screen.hpp"
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

    /* 20Hz. Was 200ms (5Hz) from the Phase 6 rework until 2026-08-25 --
     * an architectural default carried over when the old "update the
     * screen every loop iteration" model became a scheduled task, not a
     * measured choice. 5Hz did not read as smooth on real hardware. The
     * pre-rework code invalidated every panel label on every loop pass
     * and let LVGL coalesce it to its own 20ms tick, so 50Hz redraws are
     * known to have run on this panel; 20Hz is well inside that. */
    constexpr uint32_t INFO_SCREEN_REFRESH_MS = 50;

    /* Backstop cadence for re-pointing core::SignalPoller at the displayed
     * panel set. Every path that can change that set calls
     * sync_active_signals() directly, so this only ever catches a future
     * one that forgets to -- half a second is imperceptible for a change
     * the user just made by hand, and an unchanged list costs a six-entry
     * comparison. */
    constexpr uint32_t ACTIVE_SIGNAL_SYNC_MS = 500;

    /* How often LVGL polls the touch driver. lv_indev_create() defaults
     * this to LV_DEF_REFR_PERIOD (20ms, lib/lv_conf.h), which is the
     * screen redraw cadence rather than anything chosen for input -- so a
     * press could sit up to 20ms before LVGL saw it, and a drag sampled
     * that coarsely tracks visibly behind the finger.
     *
     * 10ms halves the worst-case press latency and doubles the sample
     * density along a drag, for one extra I2C transaction per 10ms
     * (5 bytes off the FT6236). loop() calls lv_timer_handler() every
     * pass with no rate limit, so there is headroom for it. */
    constexpr uint32_t TOUCH_POLL_MS              = 10;
    constexpr uint32_t WIRELESS_SCREEN_REFRESH_MS = 500;
    /* Slower than the info screen's: nothing here changes faster than
     * once a minute except the capture toggle, which repaints itself on
     * press rather than waiting for this. */
    constexpr uint32_t LOGGING_SCREEN_REFRESH_MS = 1000;

    /* How often the logger *looks*, not how often it writes -- the write
     * rate per signal is floored at core::LOG_MIN_INTERVAL_MS (50ms)
     * inside Logger::sample() itself. Matching the catalog's fastest
     * cadence (20ms, see signal_catalog.generated.inc) means a 20ms
     * signal is seen on the tick it actually arrives, so that floor
     * produces clean 40ms spacing rather than the ragged 40/60ms a
     * coarser tick would give once scheduler jitter is in play. Slower
     * signals (100ms and up) are unaffected either way -- they're logged
     * at their own arrival cadence. Sampling twice as often costs
     * essentially nothing: each tick walks at most MAX_PANELS entries. */
    /* Half core::LOG_MIN_INTERVAL_MS, preserving the relationship the
     * original 20/40 pair had: the tick has to be finer than the floor,
     * or a signal arriving exactly on the floor gets skipped whenever
     * jitter puts its arrival a hair inside the window and the next tick
     * a hair outside. Was 20 while the floor was 40. */
    constexpr uint32_t LOG_SAMPLE_PERIOD_MS = 25;

    /* RGB565 (2 bytes/pixel), NOT lv_color_t: with LV_COLOR_DEPTH 16 LVGL
     * renders into the draw buffer in the display's own color format,
     * which is what display_flush_cb() below already reinterprets these
     * as. lv_color_t is a 3-byte {b,g,r} struct regardless of
     * LV_COLOR_DEPTH, so declaring them as an array of it over-allocated
     * every buffer by 50% (46080 bytes each for 15360 pixels' worth of
     * RGB565) without LVGL ever using the extra byte per pixel.
     * Deliberately a fraction of the panel (DRAW_BUF_DIVISOR: a tenth by
     * default, a fifth on the S3), not a full framebuffer --
     * LV_DISPLAY_RENDER_MODE_PARTIAL renders and flushes in bands. Bigger
     * bands mean fewer passes over the widget tree per frame, which is
     * most of LVGL's cost on a full-screen redraw.
     * 4-byte aligned for the DMA push in TeensyDisplay::flush().
     *
     * Only the fallback: a platform whose display offers whole-screen
     * buffers (IDisplay::get_frame_buffers -- the S3, out of its PSRAM)
     * renders DIRECT into those instead, and these go unused. */
#ifndef DRAW_BUF_DIVISOR
    #define DRAW_BUF_DIVISOR 10
#endif
    alignas(4) uint16_t s_draw_buf_1[(LCD_HORZ * LCD_VERT) / DRAW_BUF_DIVISOR];
    alignas(4) uint16_t s_draw_buf_2[(LCD_HORZ * LCD_VERT) / DRAW_BUF_DIVISOR];

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
            core::ConfigStore  config_store;
            core::SignalStore  signal_store;
            core::UdsClient    uds_client;
            core::SignalPoller uds_poller;
            core::Scheduler    scheduler;
            core::Logger       logger;

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
            /* Not built by build_screens() -- created and destroyed around
             * each sniffer session (see enter_sniff()). */
            gui::SnifferScreen sniffer_screen;

            const char *signal_names[core::SIGNAL_CATALOG_COUNT];
            uint16_t    signal_ids[core::SIGNAL_CATALOG_COUNT];

            explicit Composition(Platform &platform)
                : config_store(platform.config_store)
                , uds_client(platform.can, CAN_TX_ID, CAN_RX_ID)
                , uds_poller(uds_client, platform.clock)
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

    /*------------------------------------------------------------------
     * Backlight blanking
     *
     * Two moments need the panel dark while something ugly happens on
     * it:
     *
     *   * boot -- the panel's GRAM holds power-on garbage until a first
     *     full frame lands (TeensyDisplay::begin() holds the pin low for
     *     exactly this);
     *   * an accent-color change -- which tears every screen down and
     *     builds them again (see rebuild_screens_for_theme_change), a
     *     white flash followed by SettingsScreen sliding back in.
     *
     * Each waits on its own signal, and they are deliberately different
     * signals, because what "settled" means differs by path.
     *
     * BOOT counts completed render passes. Instrumenting the simulator:
     * pass 1 is the full 480x320 initial paint, and the passes after it
     * repaint small regions as the readouts populate. Lighting up on
     * pass 1 shows the dashboard mid-populate. Counting passes is immune
     * to how long they take, which matters because the panel is far
     * slower than a dev host.
     *
     * A REBUILD waits for the display to go quiet instead. Its landing
     * screen (SettingsScreen) has no refresh task at all, so once drawn
     * it stops rendering entirely -- counting passes there would wait
     * for a pass that never comes.
     *
     * Note what does NOT work, since it looks like the obvious
     * unification: quiet-period detection on the boot path. It held only
     * while INFO_SCREEN_REFRESH_MS was 200, where the gap before the
     * readouts populated (~98ms) was shorter than the idle gaps between
     * steady-state redraws (131-198ms), leaving a window between them.
     * At 100ms that ordering inverts -- the populate gap grows to ~106ms
     * while idle gaps fall to 23-91ms -- and no threshold exists: a low
     * one fires on pass 1, a high one never fires at all. Any future
     * change to INFO_SCREEN_REFRESH_MS moves these numbers again, which
     * is why boot no longer depends on them.
     *
     * On hardware both paths also cover something LVGL cannot report:
     * the last flush's DMA is still in flight when flush() returns (see
     * TeensyDisplay::flush), so even a completed pass isn't fully on the
     * panel at the moment it is reported.
     *-----------------------------------------------------------------*/

    /* Pass 1 is the initial full paint; the ones after it are the
     * readouts filling in. Three leaves a pass of margin over the two
     * that were actually needed when measured -- cheap, at one
     * INFO_SCREEN_REFRESH_MS each, and the failure it guards against
     * (lighting mid-populate) is the visible one. */
    constexpr uint8_t BOOT_SETTLE_PASSES = 3;

    /* Rebuild path only, where the landing screen is static and this is
     * really just "has rendering stopped". Only needs to exceed the gap
     * between flushes *within* one pass (~17ms measured), so there is
     * generous margin; it is not tuned against the redraw cadence and
     * does not move with INFO_SCREEN_REFRESH_MS. */
    constexpr uint32_t BACKLIGHT_QUIET_MS = 100;

    /* Backstop for both paths: light the panel regardless once this long
     * has passed since it went dark. A signal that never arrives would
     * otherwise leave the dashboard dark indefinitely -- the one failure
     * mode this mechanism must not have. */
    constexpr uint32_t BACKLIGHT_SETTLE_MAX_MS = 750;

    /* How often the watchdog above runs. */
    constexpr uint32_t BACKLIGHT_WATCH_POLL_MS = 25;

    enum class BacklightState
    {
        Live,               /* lit, at backlight_pct() */
        AwaitingBootPasses, /* boot: off until BOOT_SETTLE_PASSES land */
        Blanked,            /* off, and the transition isn't over yet */
        Settling,           /* off -- waiting for rendering to stop */
    };

    BacklightState &backlight_state()
    {
        /* Boot starts here: the pin is already low out of
         * TeensyDisplay::begin(), and counting render passes is what
         * eventually lights it. */
        static BacklightState state = BacklightState::AwaitingBootPasses;
        return state;
    }

    /** When display_flush_cb last ran, in clock millis -- the input to
     * the quiet test. Updated on every flush, not just a pass's last, so
     * a pass still in progress keeps pushing the deadline out. */
    uint32_t &last_flush_ms()
    {
        static uint32_t ms = 0;
        return ms;
    }

    /** Completed render passes seen since boot, capped by
     * BOOT_SETTLE_PASSES. */
    uint8_t &boot_passes()
    {
        static uint8_t passes = 0;
        return passes;
    }

    /** The repeating watchdog, live while the panel is dark. */
    lv_timer_t *&backlight_settle_timer()
    {
        static lv_timer_t *timer = nullptr;
        return timer;
    }

    /** When the current Settling wait began -- the input to the backstop. */
    uint32_t &settle_started_ms()
    {
        static uint32_t ms = 0;
        return ms;
    }

    /** The brightness the panel returns to once it's lit again. */
    uint8_t &backlight_pct()
    {
        static uint8_t pct = 100;
        return pct;
    }

    /** Sets the brightness, now if the panel is lit and on the way back
     * up if it isn't -- so a slider move that lands mid-blank isn't
     * either lost or allowed to light the panel early. */
    void apply_backlight(uint8_t pct)
    {
        backlight_pct() = pct;
        if (backlight_state() == BacklightState::Live)
        {
            platform_get().display.set_backlight(pct);
        }
    }

    /** The one place the panel is ever lit. Tears down the watchdog on
     * the way out -- deleting a timer from inside its own callback is
     * supported, lv_timer_handler() guards for exactly that. */
    void light_backlight()
    {
        backlight_state() = BacklightState::Live;
        platform_get().display.set_backlight(backlight_pct());

        if (backlight_settle_timer() != nullptr)
        {
            lv_timer_t *timer        = backlight_settle_timer();
            backlight_settle_timer() = nullptr;
            lv_timer_delete(timer);
        }
    }

    /* Runs while the panel is dark. Owns the rebuild path's quiet test
     * and the backstop for both paths; boot's pass counting lives in
     * display_flush_cb, where the passes actually arrive. */
    void backlight_watch_cb(lv_timer_t *timer)
    {
        (void)timer;
        if (backlight_state() == BacklightState::Live)
        {
            return;
        }

        uint32_t now = platform_get().clock.millis();
        if (now - settle_started_ms() >= BACKLIGHT_SETTLE_MAX_MS)
        {
            light_backlight(); /* signal never arrived -- see the backstop */
            return;
        }

        if (backlight_state() == BacklightState::Settling
            && now - last_flush_ms() >= BACKLIGHT_QUIET_MS)
        {
            light_backlight();
        }
    }

    /** Starts (or restarts) the dark period: records when it began and
     * makes sure the watchdog is running.
     *
     * Safe to call from inside display_flush_cb even though that runs
     * within LVGL's own timer handler -- lv_timer_create() flags the list
     * as changed and the handler restarts its iteration (lv_timer.c). */
    void begin_backlight_wait()
    {
        settle_started_ms() = platform_get().clock.millis();
        if (backlight_settle_timer() == nullptr)
        {
            backlight_settle_timer() = lv_timer_create(
                backlight_watch_cb, BACKLIGHT_WATCH_POLL_MS, nullptr);
        }
    }

    /** Kills the light until whatever is about to happen on screen has
     * finished. Something must walk the state back to Live afterwards,
     * or the panel stays dark -- see settings_screen_loaded_cb, the only
     * caller's counterpart. */
    void blank_backlight()
    {
#if STRESS_TEST
        /* Bench stress builds leave the panel lit through the theme
         * rebuild so the transition itself can be watched.
         * settings_screen_loaded_cb is a no-op unless the state is
         * Blanked, so skipping this leaves nothing dangling. */
        return;
#endif
        backlight_state() = BacklightState::Blanked;
        platform_get().display.set_backlight_off();
        begin_backlight_wait();
    }

    void display_flush_cb(lv_display_t    *disp,
                          const lv_area_t *area,
                          uint8_t         *px_map)
    {
        /* One render pass reaches here as several flush() calls -- one
         * per band in partial mode, one per invalidated area in direct
         * mode -- and lv_display_flush_is_last() marks the one that
         * completes it. flush_end() lets a display that holds its push
         * back (DashS3Display) send the whole pass as one transfer. */
        bool last = lv_display_flush_is_last(disp);
        platform_get().display.flush(
            area->x1,
            area->y1,
            area->x2,
            area->y2,
            reinterpret_cast<const uint16_t *>(px_map));
        if (last)
        {
            platform_get().display.flush_end();
        }
        lv_display_flush_ready(disp);

        last_flush_ms() = platform_get().clock.millis();

        /* Boot counts completed passes -- see BacklightState for why it
         * counts rather than timing them, and why the rebuild path
         * doesn't. */
        if (backlight_state() == BacklightState::AwaitingBootPasses && last)
        {
            if (++boot_passes() >= BOOT_SETTLE_PASSES)
            {
                light_backlight();
            }
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

    /* core::SignalPoller holds a full screen of panels' worth of signals,
     * which is exactly what core::Config::priorities[] can describe. If
     * one grows past the other, sync_active_signals() below would silently
     * stop asking for the panels past the cut. */
    static_assert(core::SignalPoller::MAX_ACTIVE_SIGNALS >= core::MAX_PANELS,
                  "SignalPoller cannot hold every displayable panel");

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

        app.uds_poller.set_active(ids, count);
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

        /* Closes the flow-control loop: this is what frees the poller's
         * single in-flight slot so the next request can go out. */
        composition().uds_poller.on_response(byte1, byte2);
    }

    /* A rejection still ends the request it rejects, so handing these to
     * the poller is what stops a DID the ECU refuses from costing a full
     * response timeout every time it comes due. */
    void on_uds_negative_response(void *ctx, uint8_t service, uint8_t nrc)
    {
        (void)ctx;
        composition().uds_poller.on_negative_response(service, nrc);
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

    /* Fired immediately on every brightness slider move / Day-Night toggle
     * — pushes straight to the real backlight so switching (or tuning
     * either value) is actually useful mid-drive, not just a setting
     * that takes effect after leaving Settings. */
    void settings_preview_brightness_cb(void *ctx, uint8_t brightness_pct)
    {
        (void)ctx;
        apply_backlight(brightness_pct);
    }

    /* Runs on every exit from SettingsScreen — the X button and the
     * Theme/WiFi hand-offs alike — so an edit is in the store before
     * anything downstream (a screen rebuild, the phone's config page)
     * reads the store back. Persist only; settings_done_cb owns the
     * navigation, since a hand-off to Theme/WiFi must not bounce to the
     * dashboard. */
    void settings_commit_cb(void   *ctx,
                            uint8_t day_brightness_pct,
                            uint8_t night_brightness_pct,
                            uint8_t num_panels)
    {
        (void)ctx;
        /* No set_backlight() here — settings_preview_brightness_cb already
         * pushed the real backlight live on every slider move and Day/
         * Night toggle, so it's already showing whichever of day/night was
         * actually active when the screen was left. Re-applying
         * day_brightness_pct unconditionally here was wrong: it stomped
         * over a Night-mode edit with the (usually untouched, 100%) Day
         * value on every exit. */

        core::Settings &settings = composition().config_store.get().settings;

        /* Orientation isn't touched here — it's persisted immediately by
         * settings_orientation_confirm_cb() the moment Confirm is pressed
         * on the popup (see SettingsScreen's file doc comment for why:
         * the popup itself already reads as the commit step). */
        settings.day_brightness   = day_brightness_pct;
        settings.night_brightness = night_brightness_pct;
        settings.num_panels       = num_panels;
        composition().config_store.save();
        sync_active_signals();
    }

    void settings_done_cb(void *ctx)
    {
        (void)ctx;
        /* InfoScreen reflows against active_orientation() (set once at
         * boot, never touched again) rather than settings.orientation,
         * since orientation is a boot-time-only setting (see
         * platform.hpp's DisplayOrientation/set_orientation() doc
         * comments) -- the physical panel hasn't rotated just because
         * Done was pressed. */
        composition().info_screen.rebuild(
            composition().config_store.get().settings.num_panels,
            to_gui_orientation(active_orientation()));
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
            = app.logger.total_logged_ms() / 60000u; /* whole minutes,
                                                      * no hour rollover
                                                      * -- see the view
                                                      * model */
        vm.ring_capacity_bytes = app.logger.ring_capacity_bytes();
        vm.used_bytes          = app.logger.used_bytes();

        /* The one place platform::StorageMedium is translated for gui/,
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

    /* Fired the moment LoggingScreen's toggle is tapped. Persisted right
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

    /* Fired once LoggingScreen's erase popup is confirmed -- the GUI
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

    /* Keeps the capture count and recorded-minutes readouts advancing
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

#ifdef ARDUINO
    /* USB-serial log retrieval -- a small bespoke line command set,
     * sharing the USB port with sniffer mode's SLCAN (see
     * handle_usb_line() below, which routes between the two). Deliberately
     * blocking --
     * this is meant to run as its own dedicated offload action (park,
     * plug in, dump), not interleaved with driving, so freezing
     * everything else (CAN polling, GUI) for the duration is fine. */
    FLASHMEM void handle_log_command(const char *cmd, size_t len)
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
            /* Same one call LoggingScreen's erase button makes (see
             * logging_erase_cb): format the store, then rebuild rotation
             * state and the index against the empty result, leaving the
             * capture count honestly at zero. Recording is deliberately
             * NOT stopped here the way the on-screen erase stops it --
             * this command is issued from a host with the car parked, and
             * silently changing a persisted setting out from under the
             * user over a debug serial link is a surprise the GUI's own
             * confirmation popup doesn't have to be. */
            composition().logger.erase();
        }
        else if (len == 9 && memcmp(cmd, "LOGSTATUS", 9) == 0)
        {
            core::Logger &logger = composition().logger;
            Serial.print("ready=");
            Serial.print(logger.is_ready() ? 1 : 0);
            Serial.print(" session=");
            Serial.print(logger.session_id());
            Serial.print(" chunk=");
            Serial.print(logger.current_chunk_index());
            Serial.print(" capturing=");
            Serial.print(logger.is_capturing() ? 1 : 0);
            Serial.print(" logs=");
            Serial.print(logger.log_count());
            Serial.print(" recorded_ms=");
            Serial.println(logger.total_logged_ms());
        }
    }

    /*==========================================================================
     * CAN sniffer mode
     *
     * Lets tools/monitor use this dash as its USB CAN sniffer, in place of
     * the standalone env:can_sniffer board. Never entered on its own: the
     * host asks (SNIFFREQ), the panel asks the driver (Cancel/Start popup),
     * and only Start changes anything. While active the dashboard is
     * paused outright -- no UDS polling (so the capture holds none of the
     * dash's own requests), no scheduler (logging, refresh tasks, the
     * remote link) -- and the panel shows SnifferScreen instead. Stop on
     * the panel, SNIFFEND from the host, or the host closing the port all
     * end it the same way; nothing is persisted, so a reboot always comes
     * up as a normal dash.
     *
     * Host line protocol, on top of the LOG* commands and SLCAN:
     *
     *   SNIFFREQ  -> SNIFFWAIT at once when the popup goes up, then
     *                SNIFFOK (confirmed, or already active)
     *                SNIFFNO (declined on the panel, or timed out)
     *                SNIFFBUSY (panel isn't on the gauges -- in a menu)
     *   SNIFFEND  -> CR; leaves sniffer mode (or withdraws a pending ask)
     *   SNIFFSTOP <- sent unasked when Stop is tapped on the panel
     *
     * SLCAN commands are answered only while active, BELL otherwise.
     *========================================================================*/

    constexpr uint32_t SNIFF_CONFIRM_TIMEOUT_MS = 10000;
    constexpr uint32_t SNIFF_SCREEN_REFRESH_MS  = 500;
    constexpr uint32_t SNIFF_HOST_GONE_MS       = 250;

    enum class SniffState : uint8_t
    {
        Off,
        Pending, /* popup up, waiting on the driver */
        Active
    };

    /* SLCAN's bus/serial side for the dash: ICanBus's sniff_* hooks and
     * the USB port. */
    class DashSniffPort : public core::slcan::IPort
    {
        public:
            FLASHMEM bool open(uint32_t baud, bool listen_only) override
            {
                return platform_get().can.sniff_begin(baud, listen_only);
            }

            FLASHMEM void close() override
            {
                platform_get().can.sniff_end();
            }

            FLASHMEM void transmit(const SniffFrame &frame) override
            {
                platform_get().can.sniff_send(frame);
            }

            FLASHMEM void write(const char *data, size_t len) override
            {
                Serial.write(reinterpret_cast<const uint8_t *>(data), len);
            }
    };

    DashSniffPort        s_sniff_port;
    core::slcan::Session s_sniff_session(s_sniff_port);

    SniffState s_sniff_state      = SniffState::Off;
    lv_obj_t  *s_sniff_popup      = nullptr;
    lv_obj_t  *s_sniff_ring       = nullptr; /* popup's countdown */
    uint32_t   s_sniff_pending_ms = 0;

    /* frames/s, sampled at SNIFF_SCREEN_REFRESH_MS */
    uint32_t s_sniff_rate_ms = 0;
    uint32_t s_sniff_rate_rx = 0;

    /* When `Serial` last read true -- see host_gone(). */
    uint32_t s_sniff_host_ms = 0;

    FLASHMEM void usb_reply(const char *text)
    {
        Serial.print(text);
        Serial.write(core::slcan::CR);
    }

    FLASHMEM void close_sniff_popup()
    {
        if (s_sniff_popup != nullptr)
        {
            lv_msgbox_close_async(s_sniff_popup);
            s_sniff_popup = nullptr;
            s_sniff_ring  = nullptr; /* goes with it */
        }
    }

    FLASHMEM void decline_sniff()
    {
        close_sniff_popup();
        s_sniff_state = SniffState::Off;
        usb_reply("SNIFFNO");
    }

    FLASHMEM void sniff_stop_cb(void *ctx);

    FLASHMEM void enter_sniff()
    {
        Composition &app = composition();

        close_sniff_popup();
        s_sniff_session.reset();
        s_sniff_state   = SniffState::Active;
        s_sniff_rate_ms = platform_get().clock.millis();
        s_sniff_rate_rx = 0;

        app.sniffer_screen.create(runtime_theme(), sniff_stop_cb, nullptr);
        lv_screen_load(app.sniffer_screen.screen());

        usb_reply("SNIFFOK");
    }

    FLASHMEM void exit_sniff()
    {
        Composition &app = composition();

        /* Closing the SLCAN channel is what calls sniff_end(), which puts
         * the bus back exactly as begin() left it at boot. */
        s_sniff_session.reset();
        s_sniff_state = SniffState::Off;

        app.info_screen.refresh();
        lv_screen_load(app.info_screen.screen());
        app.sniffer_screen.destroy();
    }

    FLASHMEM void exit_sniff_async(void *ctx)
    {
        (void)ctx;
        if (s_sniff_state == SniffState::Active)
        {
            exit_sniff();

            /* Unprompted, unlike every other reply: the host is otherwise
             * left streaming from a dash that quietly went back to the
             * gauges. */
            usb_reply("SNIFFSTOP");
        }
    }

    /* Fires from inside the Stop button's own click handler, and
     * exit_sniff() deletes that button -- the same reentrancy
     * rebuild_screens_for_theme_change() defers around. */
    FLASHMEM void sniff_stop_cb(void *ctx)
    {
        (void)ctx;
        lv_async_call(exit_sniff_async, nullptr);
    }

    FLASHMEM void sniff_confirm_cb(lv_event_t *e)
    {
        (void)e;
        if (s_sniff_state == SniffState::Pending)
        {
            enter_sniff();
        }
    }

    FLASHMEM void sniff_cancel_cb(lv_event_t *e)
    {
        (void)e;
        if (s_sniff_state == SniffState::Pending)
        {
            decline_sniff();
        }
    }

    FLASHMEM void request_sniff()
    {
        Composition &app = composition();

        if (s_sniff_state == SniffState::Active)
        {
            usb_reply("SNIFFOK"); /* a reconnecting host -- already on */
            return;
        }
        if (s_sniff_state == SniffState::Pending)
        {
            return; /* the popup's already asking */
        }

        /* Only from the gauges. Pulling the panel out from under
         * Settings or the configurator would skip their own exit paths
         * (Settings' done callback is what saves, for one), so a menu
         * just says no and the driver retries from the gauges. */
        if (lv_screen_active() != app.info_screen.screen())
        {
            usb_reply("SNIFFBUSY");
            return;
        }

        gui::ConfirmPopup popup
            = gui::create_confirm_popup(runtime_theme(),
                                        "Start CAN sniffer?",
                                        "Gauges will pause.",
                                        "Start");
        lv_obj_add_event_cb(
            popup.confirm_btn, sniff_confirm_cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(
            popup.cancel_btn, sniff_cancel_cb, LV_EVENT_CLICKED, nullptr);

        s_sniff_popup      = popup.mbox;
        s_sniff_ring       = gui::add_popup_countdown(
            popup, runtime_theme(), SNIFF_CONFIRM_TIMEOUT_MS / 100);
        s_sniff_state      = SniffState::Pending;
        s_sniff_pending_ms = platform_get().clock.millis();
        s_sniff_host_ms    = s_sniff_pending_ms; /* the host just wrote */

        /* Tells the host a person is being asked, so it can tell this
         * wait apart from a plain SLCAN device that will never answer. */
        usb_reply("SNIFFWAIT");
    }

    FLASHMEM void refresh_sniffer_screen(uint32_t now)
    {
        SniffStats stats   = platform_get().can.sniff_stats();
        uint32_t   elapsed = now - s_sniff_rate_ms;

        gui::SnifferViewModel vm;
        vm.channel = !s_sniff_session.is_open()
                         ? gui::SnifferChannel::Closed
                     : s_sniff_session.is_listen_only()
                         ? gui::SnifferChannel::ListenOnly
                         : gui::SnifferChannel::Normal;
        vm.frames_per_sec = elapsed == 0 ? 0
                                         : (stats.rx - s_sniff_rate_rx) * 1000u
                                               / elapsed;
        vm.total_frames   = stats.rx;
        vm.dropped_frames = stats.dropped;
        composition().sniffer_screen.refresh(vm);

        s_sniff_rate_ms = now;
        s_sniff_rate_rx = stats.rx;
    }

    /* Per-loop while active: hand buffered frames to USB, but only as many
     * as its TX buffer takes without blocking. A host that stops reading
     * then backs frames up into the sniff ring (counted as dropped once
     * it's full) rather than freezing loop() -- and with it the Stop
     * button -- inside Serial.write().
     *
     * FLASHMEM despite running per frame, as does everything else on this
     * path but the ISR: RAM1 code sits a few hundred bytes under a 32 KB
     * ITCM block boundary, and crossing it costs 32 KB of RAM1 data. It
     * only ever runs with the dashboard paused, and a loop this small
     * stays resident in the I-cache. */
    FLASHMEM void pump_sniff()
    {
        if (!s_sniff_session.is_open())
        {
            return;
        }

        ICanBus   &can = platform_get().can;
        SniffFrame frame;
        while (Serial.availableForWrite()
                   >= static_cast<int>(core::slcan::ENCODED_MAX)
               && can.sniff_read(frame))
        {
            char   line[core::slcan::ENCODED_MAX];
            size_t n
                = core::slcan::encode(frame, s_sniff_session.timestamps(), line);
            Serial.write(reinterpret_cast<const uint8_t *>(line), n);
        }
    }

    /** Only called while not Off -- loop() checks that itself, so the
     * normal dash path pays for one compare, not a flash call.
     *
     * @returns True while sniffing -- loop() skips the dashboard.
     *
     * noinline, like handle_usb_line(): each has exactly one caller in
     * ITCM code, and GCC will happily inline a FLASHMEM function into it
     * -- which lands the body right back in ITCM. */
    /* True once the host has been gone for SNIFF_HOST_GONE_MS. `Serial`
     * alone is too twitchy: Teensy's operator bool reads false for 15 ms
     * after *any* DTR/RTS change, and opening the port (pyserial raises
     * DTR, then RTS) is exactly such a change -- a SNIFFREQ sent straight
     * after open would otherwise see the host "leave" and be declined
     * before the popup was even drawn. */
    FLASHMEM bool host_gone(uint32_t now)
    {
        if (Serial)
        {
            s_sniff_host_ms = now;
            return false;
        }
        return now - s_sniff_host_ms >= SNIFF_HOST_GONE_MS;
    }

    FLASHMEM __attribute__((noinline)) bool sniff_tick()
    {
        uint32_t now = platform_get().clock.millis();

        if (s_sniff_state == SniffState::Pending)
        {
            if (host_gone(now)
                || now - s_sniff_pending_ms >= SNIFF_CONFIRM_TIMEOUT_MS)
            {
                decline_sniff();
            }
            else if (s_sniff_ring != nullptr)
            {
                /* Tenths of a second; lv_arc_set_value() ignores a repeat,
                 * so this redraws the ring ten times a second at most. */
                lv_arc_set_value(s_sniff_ring,
                                 (SNIFF_CONFIRM_TIMEOUT_MS
                                  - (now - s_sniff_pending_ms))
                                     / 100);
            }
            return false;
        }

        /* DTR drops when the host closes the port or the cable comes
         * out -- either way nobody is listening any more. */
        if (host_gone(now))
        {
            exit_sniff();
            return false;
        }

        pump_sniff();

        if (now - s_sniff_rate_ms >= SNIFF_SCREEN_REFRESH_MS)
        {
            refresh_sniffer_screen(now);
        }
        return true;
    }

    FLASHMEM __attribute__((noinline)) void handle_usb_line(const char *cmd,
                                                            size_t      len)
    {
        bool log_cmd = len >= 3 && memcmp(cmd, "LOG", 3) == 0;

        if (len == 8 && memcmp(cmd, "SNIFFREQ", 8) == 0)
        {
            request_sniff();
        }
        else if (len == 8 && memcmp(cmd, "SNIFFEND", 8) == 0)
        {
            if (s_sniff_state == SniffState::Active)
            {
                exit_sniff();
            }
            else if (s_sniff_state == SniffState::Pending)
            {
                close_sniff_popup();
                s_sniff_state = SniffState::Off;
            }
            Serial.write(core::slcan::CR);
        }
        else if (s_sniff_state == SniffState::Active && !log_cmd)
        {
            /* LOG* excluded explicitly: "LOGDUMP" would otherwise read as
             * SLCAN's 'L' (open listen-only). */
            s_sniff_session.handle(cmd, len);
        }
        else if (s_sniff_state == SniffState::Off && log_cmd)
        {
            handle_log_command(cmd, len);
        }
        else
        {
            Serial.write(core::slcan::BELL);
        }
    }

    void poll_usb_serial()
    {
        static char   cmd[core::slcan::COMMAND_MAX + 1];
        static size_t cmd_len = 0;

        while (Serial.available() > 0)
        {
            char c = static_cast<char>(Serial.read());
            if (c == '\r' || c == '\n')
            {
                /* A bare CR is an SLCAN no-op that still wants its CR
                 * back; a bare LF is just the tail of a CRLF. */
                if (cmd_len > 0
                    || (c == '\r' && s_sniff_state == SniffState::Active))
                {
                    handle_usb_line(cmd, cmd_len);
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

        /* Every screen is about to be deleted and rebuilt, which is not
         * something the panel should be showing: the blank transition
         * screen destroy_screens() loads reads as a white flash, and
         * SettingsScreen then slides back in from the top. Killed here
         * rather than inside the rebuild so the panel is already dark
         * before the swatch's own press-release even finishes drawing.
         * settings_screen_loaded_cb brings it back. */
        blank_backlight();

        lv_async_call(rebuild_screens_for_theme_change, nullptr);
    }

    /* The other half of theme_picker_done_cb's blank_backlight(). Fires
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
        if (backlight_state() != BacklightState::Blanked)
        {
            return;
        }

        /* Guarded above so a second SCREEN_LOADED -- the event fires on
         * every trip into Settings -- can't restart the wait for a panel
         * that is already lit. */
        backlight_state() = BacklightState::Settling;
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

    /* Fired by RemoteConfigServer once a phone-side Save has validated,
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
        apply_backlight(
            composition().config_store.get().settings.day_brightness);

        /* A remote apply can rewrite priorities[] and num_panels wholesale,
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

        /* Same treatment as an accent change: the rebuild flashes a blank
         * transition screen, so kill the backlight first and let
         * settings_screen_loaded_cb bring it back once the rebuilt
         * SettingsScreen has finished its slide-in. */
        blank_backlight();
        lv_async_call(rebuild_screens_for_theme_change, nullptr);
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
                                 to_gui_orientation(settings.orientation),
                                 settings_commit_cb,
                                 nullptr,
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

        /* Re-seed from the saved config rather than assuming the caller
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
#ifdef ARDUINO
    Serial.begin(115200);
#endif

    Platform &platform = platform_get();

    /* Loaded before display/touch begin() so settings.orientation is
     * known up front -- orientation is a boot-time-only choice (see
     * platform.hpp's DisplayOrientation doc comments), and the one
     * lv_display_t created below has to be sized correctly from the
     * start since nothing resizes it live. */
    Composition    &app      = composition();
    core::Settings &settings = app.config_store.get().settings;
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

    /* Whole-screen buffers from the platform if it has them (see
     * s_draw_buf_1's comment), else the banded partial pair here. */
    uint16_t *frame_buf_1;
    uint16_t *frame_buf_2;
    size_t    frame_buf_bytes;
    if (platform.display.get_frame_buffers(
            frame_buf_1, frame_buf_2, frame_buf_bytes))
    {
        lv_display_set_buffers(display,
                               frame_buf_1,
                               frame_buf_2,
                               frame_buf_bytes,
                               LV_DISPLAY_RENDER_MODE_DIRECT);
    }
    else
    {
        lv_display_set_buffers(display,
                               s_draw_buf_1,
                               s_draw_buf_2,
                               sizeof(s_draw_buf_1),
                               LV_DISPLAY_RENDER_MODE_PARTIAL);
    }

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_timer_set_period(lv_indev_get_read_timer(indev), TOUCH_POLL_MS);

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
    runtime_theme().metrics        = orientation == DisplayOrientation::Portrait
                                         ? gui::METRICS_PORTRAIT
                                         : gui::THEME_DEFAULT.metrics;

    app.remote_server.set_applied_callback(remote_config_applied_cb, nullptr);

    build_screens();

    /* No per-signal scheduler task any more -- core::SignalPoller paces
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

    /* Arms the backstop for the boot blank. The panel is already dark
     * (TeensyDisplay::begin() holds the pin low) and display_flush_cb's
     * pass counting is what normally lights it -- this is purely so a
     * boot where those passes never arrive still ends with a lit
     * dashboard rather than a black one. Has to come after lv_init(),
     * since it creates an lv_timer. */
    begin_backlight_wait();

    lv_screen_load(app.info_screen.screen());
}

void loop()
{
    Composition &app = composition();

#ifdef ARDUINO
    poll_usb_serial();

    /* Sniffer mode pauses the whole dashboard -- see its block comment. */
    if (s_sniff_state != SniffState::Off && sniff_tick())
    {
        lv_timer_handler();
        return;
    }
#endif

    app.scheduler.tick();

    /* Drain first, send second: a response landing this iteration frees
     * the poller's in-flight slot, so tick() can put the next request on
     * the wire in the same pass instead of a loop later. */
    app.uds_client.poll(
        on_uds_response, nullptr, on_uds_negative_response, nullptr);
    app.uds_poller.tick();

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
