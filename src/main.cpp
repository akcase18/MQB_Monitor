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

#include "auto_brightness.hpp"
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
#include "remote_ota_controller.hpp"

#include "configurator_screen.hpp"
#include "firmware_update_screen.hpp"
#include "info_screen.hpp"
#include "logging_screen.hpp"
#include "orientation_screen.hpp"
#include "remote_binding.hpp"
#include "screen_helpers.hpp"
#include "settings_screen.hpp"
#include "signal_picker_screen.hpp"
#include "sniffer_screen.hpp"
#include "theme_picker_screen.hpp"
#include "vehicle_binding.hpp"
#include "warning_config_screen.hpp"
#include "wifi_indicator.hpp"
#include "wireless_setup_screen.hpp"

#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>
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

    /**
     * How often Auto brightness steps its ramp toward the car's dimmer
     * level -- fine enough that core::AutoBrightness::RAMP_PCT_PER_S moves
     * in 4% steps, which don't read as steps.
     */
    constexpr uint32_t AUTO_BRIGHTNESS_PERIOD_MS = 40;

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
    /* Firmware update screen: quick enough that the bar visibly moves
     * through a ~15s upload and the Install popup follows the upload's
     * end without a noticeable gap. */
    constexpr uint32_t FIRMWARE_UPDATE_REFRESH_MS = 200;
    /* How long a freshly installed firmware has to run normally -- loop()
     * coming round under the hang watchdog, frames reaching the panel,
     * the relay up (platform_update_path_ready()) -- before it is declared
     * good and the previous one stops being the fallback (see
     * platform_firmware_healthy()). */
    constexpr uint32_t FIRMWARE_HEALTHY_AFTER_MS = 10 * 1000;
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

    /* core::Logger's ring, in CHUNK_SIZE_BYTES chunks. A board with less
     * room for logs than the default 13MiB sets it from platformio.ini --
     * env:dash_s3 does, to fit its second OTA app slot. The ring never
     * asks the storage how big it is (see TODO.md), so this has to agree
     * with the partition by hand. */
#ifndef LOG_CHUNK_COUNT
    #define LOG_CHUNK_COUNT core::CHUNK_COUNT
#endif
    alignas(4) uint16_t s_draw_buf_1[(LCD_HORZ * LCD_VERT) / DRAW_BUF_DIVISOR];
    alignas(4) uint16_t s_draw_buf_2[(LCD_HORZ * LCD_VERT) / DRAW_BUF_DIVISOR];

    /**
     * Every long-lived object the app needs, wired together once. Lazily
     * constructed on first access (Meyer's singleton, matching the pattern
     * platform's HAL classes already use) so it can safely depend on
     * platform_get() without any static-initialization-order concerns, and
     * so setup()/loop()/the LVGL callbacks below can all reach the same
     * instance. Static storage everywhere but the ESP32, where it's one
     * allocation at first access (see composition()).
     */
    struct Composition
    {
            core::ConfigStore  config_store;
            core::SignalStore  signal_store;
            core::UdsClient    uds_client;
            core::SignalPoller uds_poller;
            core::Scheduler    scheduler;
            core::Logger       logger;

            remote::server::RemoteConfigServer  remote_server;
            remote::server::RemoteConfigLink    remote_link;
            remote::server::RemoteApController  remote_ap;
            remote::server::RemoteOtaController remote_ota;

            gui::VehicleBinding vehicle_binding;
            gui::RemoteBinding  remote_binding;

            gui::InfoScreen           info_screen;
            gui::LoggingScreen        logging_screen;
            gui::ConfiguratorScreen   configurator_screen;
            gui::SignalPickerScreen   signal_picker_screen;
            gui::WarningConfigScreen  warning_screen;
            gui::SettingsScreen       settings_screen;
            gui::ThemePickerScreen    theme_picker_screen;
            gui::OrientationScreen    orientation_screen;
            gui::WirelessSetupScreen  wireless_setup_screen;
            gui::FirmwareUpdateScreen firmware_update_screen;
            gui::WifiIndicator        wifi_indicator;
            /* Not built by build_screens() -- created and destroyed around
             * each sniffer session (see enter_sniff()). */
            gui::SnifferScreen sniffer_screen;

            const char *signal_names[core::SIGNAL_CATALOG_COUNT];
            uint16_t    signal_ids[core::SIGNAL_CATALOG_COUNT];
            bool        signal_is_state[core::SIGNAL_CATALOG_COUNT];

            /* FLASHMEM: runs once, and its member initializers are a
             * kilobyte of code RAM1 can't spare. */
            FLASHMEM explicit Composition(Platform &platform)
                : config_store(platform.config_store)
                , uds_client(platform.can, core::UDS_ECUS, core::UDS_ECU_COUNT)
                , uds_poller(uds_client, platform.clock)
                , scheduler(platform.clock)
                , logger(
                      platform.storage, core::CHUNK_SIZE_BYTES, LOG_CHUNK_COUNT)
                , remote_server(config_store, signal_store)
                , remote_link(platform.serial, remote_server)
                , remote_ap(remote_link, platform.clock)
                , remote_ota(remote_link)
                , vehicle_binding(signal_store, config_store)
                , remote_binding(remote_ap, remote_ota)
            {
                for (uint16_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
                {
                    signal_names[i] = core::SIGNAL_CATALOG[i].name;
                    signal_ids[i]   = core::SIGNAL_CATALOG[i].id;
                    signal_is_state[i]
                        = core::signal_is_state(core::SIGNAL_CATALOG[i]);
                }
            }
    };

    Composition &composition()
    {
#if defined(ARDUINO_ARCH_ESP32)
        /**
         * In PSRAM on the S3: ~15KB the Wi-Fi stack needs more in internal
         * RAM. See psram_new().
         */
        static Composition &instance = psram_new<Composition>(platform_get());
#else
        static Composition instance(platform_get());
#endif
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
     * life of the process. A local layout change restarts the board (see
     * orientation_layout_confirm_cb()), but a phone-side one only
     * rewrites settings.orientation, leaving it ahead of the physical
     * panel until the next boot (orientation is boot-time-only, see
     * platform.hpp's DisplayOrientation) -- anywhere InfoScreen gets
     * rebuilt live must read this, not settings.orientation, or it would
     * reflow against tomorrow's orientation on today's still-unrotated
     * canvas.
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

    /** The 180-degree flag belonging to `orientation` -- each layout
     * keeps its own, so rotating one never turns the other over. */
    bool &flipped_for(core::Settings         &settings,
                      core::LayoutOrientation orientation)
    {
        return orientation == core::LayoutOrientation::Vertical
                   ? settings.flipped_vertical
                   : settings.flipped_horizontal;
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
     * A REBUILD landing on SettingsScreen waits for the display to go
     * quiet instead. That screen has no refresh task at all, so once
     * drawn it stops rendering entirely -- counting passes there would
     * wait for a pass that never comes. A rebuild landing on the
     * dashboard (a phone previewing a new accent) is boot's situation
     * again, and counts passes like boot does.
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
        AwaitingBootPasses, /* boot, or a rebuild landing on the
                             * dashboard: off until BOOT_SETTLE_PASSES
                             * land */
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

    /**
     * The brightness the panel returns to once it's lit again. setup()
     * seeds it from the saved mode before the first frame lights the
     * panel: left at this initializer, every boot came up at 100%
     * whatever was saved, until something touched brightness.
     */
    uint8_t &backlight_pct()
    {
        static uint8_t pct = 100;
        return pct;
    }

    /**
     * What the backlight follows right now: the saved
     * Settings::brightness_mode, except while SettingsScreen or the phone
     * is previewing another. Change it through set_live_brightness_mode().
     */
    core::BrightnessMode &live_brightness_mode()
    {
        static core::BrightnessMode mode = core::BrightnessMode::Day;
        return mode;
    }

    /**
     * Auto's slider position right now (Settings::auto_brightness_pct, or
     * SettingsScreen's/the phone's unsaved one while it's being dragged).
     */
    uint8_t &live_auto_pct()
    {
        static uint8_t pct = core::AUTO_BRIGHTNESS_NEUTRAL;
        return pct;
    }

    core::AutoBrightness &auto_brightness()
    {
        static core::AutoBrightness auto_level;
        return auto_level;
    }

    /**
     * The slider value `mode` uses: its level for Day/Night, the shift's
     * position for Auto.
     */
    FLASHMEM uint8_t mode_slider_pct(const core::Settings &settings,
                                     core::BrightnessMode  mode)
    {
        switch (mode)
        {
            case core::BrightnessMode::Night:
                return settings.night_brightness;
            case core::BrightnessMode::Auto:
                return settings.auto_brightness_pct;
            case core::BrightnessMode::Day:
            default:
                return settings.day_brightness;
        }
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
     * which is exactly what core::Config::priorities[] can describe, plus
     * Dim Ctrl for Auto brightness. If one grows past the other,
     * sync_active_signals() below would silently stop asking for the
     * signals past the cut. */
    static_assert(core::SignalPoller::MAX_ACTIVE_SIGNALS >= core::MAX_PANELS + 1,
                  "SignalPoller cannot hold every displayable panel");

    /** Points the poller at the signals the dashboard is currently showing
     * -- `priorities[0..num_panels-1]` -- plus, in Auto brightness, the
     * cluster's dimming level (core::AutoBrightness). That is the complete
     * set of live values anything reads (gui::VehicleBinding::
     * build_view_model() for the panels, core::Logger::sample() for the
     * log, auto_brightness_task, and nothing else). Every other catalog
     * entry is dead weight on the bus until the user puts it on a panel.
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

        uint16_t ids[core::MAX_PANELS + 1];
        for (uint8_t i = 0; i < count; i++)
        {
            ids[i] = cfg.priorities[i].signal_id;
        }
        if (live_brightness_mode() == core::BrightnessMode::Auto)
        {
            ids[count++] = core::AutoBrightness::DIM_SIGNAL_ID;
        }

        app.uds_poller.set_active(ids, count);
    }

    /**
     * Switches what the backlight follows. `slider_pct` is the slider for
     * that mode (mode_slider_pct()), passed in rather than read from the
     * settings since it may not be saved yet (a slider mid-drag): Day's
     * or Night's level, applied now, or Auto's shift, which
     * auto_brightness_task ramps to from wherever the panel is.
     */
    FLASHMEM void set_live_brightness_mode(core::BrightnessMode mode,
                                           uint8_t              slider_pct)
    {
        bool entering_auto = mode == core::BrightnessMode::Auto
                             && live_brightness_mode() != mode;
        live_brightness_mode() = mode;
        if (mode != core::BrightnessMode::Auto)
        {
            apply_backlight(slider_pct);
        }
        else
        {
            live_auto_pct() = slider_pct;
            if (entering_auto)
            {
                auto_brightness().start(backlight_pct(),
                                        platform_get().clock.millis());
            }
        }
        sync_active_signals();
    }

    /**
     * Auto brightness: steps the backlight toward the cluster's dimming
     * level, or toward the Day level while the cluster isn't answering.
     * While the panel is dark (booting, or a screen rebuild) it jumps
     * straight there instead, so a night-time boot doesn't light at the
     * Day level and fade down.
     */
    FLASHMEM void auto_brightness_task(void *ctx)
    {
        (void)ctx;
        if (live_brightness_mode() != core::BrightnessMode::Auto)
        {
            return;
        }

        Composition &app = composition();
        static const int dim_idx
            = core::signal_table_find(core::AutoBrightness::DIM_SIGNAL_ID);
        uint8_t pct = auto_brightness().update(
            app.signal_store.get_at(dim_idx), /* no data if not compiled */
            core::AutoBrightness::shift_for(live_auto_pct()),
            app.config_store.get().settings.day_brightness,
            platform_get().clock.millis(),
            backlight_state() != BacklightState::Live);
        if (pct != backlight_pct())
        {
            apply_backlight(pct);
        }
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

        /* Paused for a firmware update. The relay is erasing and writing
         * flash on the other core, which stalls this one on every
         * operation anyway, and an update ends in a restart. */
        if (app.remote_ota.state() != remote::server::OtaState::Idle)
        {
            return;
        }

        app.logger.sample(app.signal_store,
                          app.config_store.get(),
                          platform_get().clock.millis());
    }

    void on_uds_response(void          *ctx,
                         uint8_t        ecu,
                         uint8_t        byte1,
                         uint8_t        byte2,
                         const uint8_t *payload,
                         uint8_t        len)
    {
        (void)ctx;
        composition().signal_store.on_uds_response(
            ecu, byte1, byte2, payload, len, platform_get().clock.millis());

        /* Closes the flow-control loop: this is what frees the poller's
         * single in-flight slot so the next request can go out. */
        composition().uds_poller.on_response(ecu, byte1, byte2);
    }

    /* A rejection still ends the request it rejects, so handing these to
     * the poller is what stops a DID the ECU refuses from costing a full
     * response timeout every time it comes due. */
    void on_uds_negative_response(void   *ctx,
                                  uint8_t ecu,
                                  uint8_t service,
                                  uint8_t nrc)
    {
        (void)ctx;
        composition().uds_poller.on_negative_response(ecu, service, nrc);
    }

    /** Whether a revert in progress came from the panel (the AP going
     * down) rather than from the phone -- see remote_config_event_cb. */
    bool &reverting_locally()
    {
        static bool local = false;
        return local;
    }

    /** Whether a revert in progress is for the phone having gone -- off
     * the AP (phone_left()) or silent past REMOTE_SESSION_TIMEOUT_MS --
     * and should land on the Wi-Fi screen's QR code, as the page closing
     * does (ConfigEvent::PageLeft). */
    bool &reverting_for_lost_phone()
    {
        static bool lost = false;
        return lost;
    }

    /** Throws away the phone's unsaved preview, if there is one. For when
     * the session ends without a Save: the panel's X, or the AP timing
     * out. */
    FLASHMEM void revert_remote_preview()
    {
        reverting_locally() = true;
        composition().remote_server.revert_preview();
        reverting_locally() = false;
    }

    /* How long the phone's page can go without a request before its
     * preview session is ended for it. The page pings well inside this
     * while it's open (see index.html); a phone that walked out of range,
     * or a browser killed without its pagehide firing, sends nothing --
     * and the panel shouldn't go on showing sample values for the rest of
     * the AP's idle timeout. */
    constexpr uint32_t REMOTE_SESSION_TIMEOUT_MS = 60 * 1000;

    /** When the phone last changed anything -- see
     * REMOTE_SESSION_TIMEOUT_MS. */
    uint32_t &remote_last_request_ms()
    {
        static uint32_t ms = 0;
        return ms;
    }

    /** The settings as of the last phone-side change (or the AP coming
     * up) -- what a preview is compared against to see which brightness
     * it touched. */
    core::Settings &remote_last_settings()
    {
        static core::Settings settings {};
        return settings;
    }

    /** Which brightness a phone-side change shows: the level it last
     * edited, else the mode it picked. See remote_config_event_cb. */
    core::BrightnessMode &remote_shown_mode()
    {
        static core::BrightnessMode mode = core::BrightnessMode::Day;
        return mode;
    }

    /* The Wi-Fi glyph follows the AP: shown only while it's Active.
     * Called wherever the AP state can change, and after a screen
     * rebuild, which recreates the glyph hidden. */
    FLASHMEM void sync_wifi_indicator()
    {
        Composition &app = composition();
        app.wifi_indicator.set_state(
            app.remote_ap.state() == remote::server::ApState::Active,
            app.remote_ap.station_count() > 0);
    }

    /* The last phone left the AP, which stays up. Whatever it was
     * previewing goes, and the dashboard it left behind -- previewing, or
     * back on live values after its page closed -- gives way to the QR
     * code, ready for the next one. Anywhere else, someone at the panel
     * is using it: stay put. */
    FLASHMEM void phone_left()
    {
        Composition &app = composition();
        if (app.remote_server.preview_active())
        {
            /* remote_config_event_cb does the navigating. */
            reverting_for_lost_phone() = true;
            revert_remote_preview();
            reverting_for_lost_phone() = false;
        }
        else if (lv_screen_active() == app.info_screen.screen())
        {
            app.wireless_setup_screen.open(
                app.remote_binding.build_view_model(),
                app.settings_screen.screen());
        }
    }

    /* Request/response pairs are handled internally by
     * RemoteConfigLink::poll(); this only ever sees the async EVT_*
     * messages -- the AP's and a firmware update's. Each controller
     * ignores the other's. */
    FLASHMEM void remote_evt_cb(void                     *ctx,
                                remote::protocol::MsgType type,
                                const uint8_t            *payload,
                                uint16_t                  len)
    {
        (void)ctx;
        Composition &app    = composition();
        uint32_t     now    = platform_get().clock.millis();
        uint8_t      phones = app.remote_ap.station_count();
        app.remote_ap.on_event(type, payload, len, now);
        app.remote_ota.on_event(type, payload, len, now);
        sync_wifi_indicator();

        /* Only while the AP is still up: its going down zeroes the count
         * too, and has its own handling (remote_ap_idle_check_task). */
        if (phones > 0 && app.remote_ap.station_count() == 0
            && app.remote_ap.state() == remote::server::ApState::Active)
        {
            phone_left();
        }
    }

    /* Registered once at remote::server::AP_IDLE_CHECK_PERIOD_MS via
     * core::Scheduler — a no-op unless RemoteApController actually has a
     * timeout to act on (see remote::server::RemoteApController::tick()). */
    FLASHMEM void remote_ap_idle_check_task(void *ctx)
    {
        (void)ctx;
        Composition &app = composition();
        app.remote_ap.tick(platform_get().clock.millis());
        sync_wifi_indicator();

        /* The AP timed out (or otherwise went down) with the phone's
         * edits still unsaved -- nobody is left to Save them. Keyed on the
         * AP *leaving* Active rather than on it not being Active, so this
         * runs once per AP session, not on every check while it's down. */
        static bool was_active = false;
        bool active = app.remote_ap.state() == remote::server::ApState::Active;
        if (was_active && !active)
        {
            revert_remote_preview();
            /* Update mode lives on the AP too: the phone that asked for
             * it is gone. Each is a no-op outside its own state. */
            app.remote_ota.decline();
            app.remote_ota.leave();
        }
        was_active = active;

        if (app.remote_server.preview_active()
            && platform_get().clock.millis() - remote_last_request_ms()
                   > REMOTE_SESSION_TIMEOUT_MS)
        {
            reverting_for_lost_phone() = true;
            revert_remote_preview();
            reverting_for_lost_phone() = false;
        }
    }

    gui::PanelViewModel view_model_builder(void *ctx, uint8_t rank)
    {
        auto *binding = static_cast<gui::VehicleBinding *>(ctx);
        return binding->build_view_model(rank);
    }

    /** While the phone holds an unsaved preview, the dashboard is showing
     * the phone's edits, not something to edit locally -- any local save()
     * would quietly persist them. A tap or swipe instead goes back to the
     * Wireless screen, whose X is the way out (and reverts). */
    FLASHMEM __attribute__((noinline)) bool divert_to_wireless_if_previewing()
    {
        Composition &app = composition();
        if (!app.remote_server.preview_active())
        {
            return false;
        }
        app.wireless_setup_screen.open(app.remote_binding.build_view_model(),
                                       app.settings_screen.screen());
        return true;
    }

    void panel_pressed_cb(void *ctx, uint8_t rank)
    {
        (void)ctx;
        if (divert_to_wireless_if_previewing())
        {
            return;
        }
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

    /* Fired immediately on every brightness slider move / mode toggle
     * — pushes straight to the real backlight so switching (or tuning
     * either value) is actually useful mid-drive, not just a setting
     * that takes effect after leaving Settings. */
    void settings_preview_brightness_cb(void                *ctx,
                                        gui::BrightnessMode  mode,
                                        uint8_t              brightness_pct)
    {
        (void)ctx;
        set_live_brightness_mode(gui::to_core_brightness_mode(mode),
                                 brightness_pct);
    }

    /* Runs on every exit from SettingsScreen — the X button and the
     * Theme/WiFi hand-offs alike — so an edit is in the store before
     * anything downstream (a screen rebuild, the phone's config page)
     * reads the store back. Persist only; settings_done_cb owns the
     * navigation, since a hand-off to Theme/WiFi must not bounce to the
     * dashboard. */
    void settings_commit_cb(void               *ctx,
                            uint8_t             day_brightness_pct,
                            uint8_t             night_brightness_pct,
                            uint8_t             auto_brightness_pct,
                            gui::BrightnessMode brightness_mode,
                            uint8_t             num_panels)
    {
        (void)ctx;
        /* No set_backlight() here — settings_preview_brightness_cb already
         * pushed the real backlight live on every slider move and mode
         * toggle, so it's already showing whichever mode was actually
         * active when the screen was left. Re-applying
         * day_brightness_pct unconditionally here was wrong: it stomped
         * over a Night-mode edit with the (usually untouched, 100%) Day
         * value on every exit. */

        core::Settings &settings = composition().config_store.get().settings;

        /* Orientation isn't touched here — OrientationScreen persists
         * its own changes (see the orientation_* callbacks below). */
        settings.day_brightness   = day_brightness_pct;
        settings.night_brightness = night_brightness_pct;
        settings.auto_brightness_pct = auto_brightness_pct;
        settings.brightness_mode
            = gui::to_core_brightness_mode(brightness_mode);
        settings.num_panels = num_panels;
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

    FLASHMEM void settings_pick_orientation_cb(void *ctx)
    {
        (void)ctx;
        Composition &app = composition();
        /* active_orientation(), not settings.orientation -- the screen
         * should show the layout the panel is actually in, not one a
         * phone has staged for the next boot. */
        app.orientation_screen.open(to_gui_orientation(active_orientation()),
                                    app.settings_screen.screen());
    }

    /* Fired when Confirm is pressed on OrientationScreen's layout popup.
     * Swapping width and height can't happen live -- the lv_display_t and
     * every screen are sized from it once, at boot -- so this persists
     * the pick and restarts, which is what the popup told the user. */
    FLASHMEM void orientation_layout_confirm_cb(
        void *ctx, gui::LayoutOrientation orientation)
    {
        (void)ctx;
        core::Settings &settings = composition().config_store.get().settings;
        settings.orientation     = to_core_orientation(orientation);
        /* Comes back up on OrientationScreen, so the result can be checked
         * (or undone) without navigating back to it. */
        settings.open_orientation_on_boot = true;
        composition().config_store.save();

        /* Dark from here on: the reset itself, then boot, which keeps the
         * panel off until its first frames have landed in the new
         * orientation (see BacklightState). */
        platform_get().display.set_backlight_off();
        platform_restart();
    }

    /** Whether the panel is turned 180 degrees right now. Set at boot
     * and by apply_panel_flip(), so a phone-side change can tell whether
     * the flag it brings differs from what's on the glass. */
    bool &panel_flipped()
    {
        static bool instance = false;
        return instance;
    }

    /* Unlike a layout change, a 180-degree turn applies live: it keeps
     * the resolution, so nothing needs rebuilding -- the panel and touch
     * are re-rotated in place and the current screen redrawn. */
    FLASHMEM void apply_panel_flip(bool flipped)
    {
        panel_flipped() = flipped;

        /* What's in the panel's GRAM is upside down the moment MADCTL
         * changes, until the redraw below replaces it -- not something to
         * show. */
        blank_backlight();

        DisplayOrientation orientation
            = to_display_orientation(to_gui_orientation(active_orientation()));
        platform_get().display.set_orientation(orientation, flipped);
        platform_get().touch.set_orientation(orientation, flipped);

        /* Redrawn now rather than on the next refresh tick, so the quiet
         * test below starts counting from a flush that has actually
         * happened -- otherwise the watchdog could find the display
         * "quiet" before the redraw even began and light the stale
         * frame. Safe from an event handler: flush completes
         * synchronously (display_flush_cb calls flush_ready itself). */
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(nullptr);

        /* No load animation to wait out here, so straight to the same
         * quiet-period settle the theme rebuild ends on. Guarded for
         * STRESS_TEST builds, where blank_backlight() leaves it lit. */
        if (backlight_state() == BacklightState::Blanked)
        {
            backlight_state() = BacklightState::Settling;
        }
    }

    /* Fired on every tap of OrientationScreen's Rotate 180. */
    FLASHMEM void orientation_rotate_cb(void *ctx)
    {
        (void)ctx;
        core::Settings &settings = composition().config_store.get().settings;
        /* active_orientation(), not settings.orientation: the flag to
         * toggle is the one for the layout the panel is actually in. */
        bool &flipped = flipped_for(settings, active_orientation());
        flipped       = !flipped;
        composition().config_store.save();
        apply_panel_flip(flipped);
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
     * orientation_rotate_cb/theme_picker_done_cb) and pushed
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

    /** core::Logger::erase(), with the hang watchdog off around it:
     * reformatting the whole log partition can block for longer than the
     * watchdog allows. noinline: its callers sit in ITCM, and GCC would
     * otherwise inline it right back there. */
    FLASHMEM __attribute__((noinline)) void erase_logs()
    {
        platform_hang_watchdog(false);
        composition().logger.erase();
        platform_hang_watchdog(true);
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

        erase_logs();
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
            /* Megabytes over USB: far longer than the hang watchdog
             * allows loop() -- and blocking here is the point. */
            platform_hang_watchdog(false);

            IStorage &storage = platform_get().storage;
            for (uint16_t i = 0; i < LOG_CHUNK_COUNT; i++)
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

            platform_hang_watchdog(true);
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
            erase_logs();
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

        s_sniff_popup = popup.mbox;
        s_sniff_ring  = gui::add_popup_countdown(
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
        vm.channel = !s_sniff_session.is_open() ? gui::SnifferChannel::Closed
                     : s_sniff_session.is_listen_only()
                         ? gui::SnifferChannel::ListenOnly
                         : gui::SnifferChannel::Normal;
        vm.frames_per_sec
            = elapsed == 0 ? 0 : (stats.rx - s_sniff_rate_rx) * 1000u / elapsed;
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
            size_t n = core::slcan::encode(
                frame, s_sniff_session.timestamps(), line);
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
                lv_arc_set_value(
                    s_sniff_ring,
                    (SNIFF_CONFIRM_TIMEOUT_MS - (now - s_sniff_pending_ms))
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

    /** The accent every screen was last built against -- a config whose
     * accent differs needs a rebuild before it shows. */
    uint32_t &built_accent()
    {
        static uint32_t accent = 0;
        return accent;
    }

    /** Where a queued rebuild lands. */
    enum class RebuildTarget : uint8_t
    {
        Settings,
        Dashboard,
        Wireless, /* the QR code, after the phone left -- phone_left() */
    };

    RebuildTarget &rebuild_target()
    {
        static RebuildTarget target = RebuildTarget::Settings;
        return target;
    }

    bool &rebuild_pending()
    {
        static bool pending = false;
        return pending;
    }

    /**
     * Darkens the panel and queues a rebuild of every screen against the
     * config's current accent. Several requests before it runs (a phone
     * tapping through swatches) cost one rebuild, landing wherever the
     * last one asked for.
     */
    FLASHMEM void request_rebuild(RebuildTarget target)
    {
        rebuild_target() = target;
        if (rebuild_pending())
        {
            return;
        }
        rebuild_pending() = true;

        /* Every screen is about to be deleted and rebuilt, which is not
         * something the panel should be showing: the blank transition
         * screen destroy_screens() loads reads as a white flash. Killed
         * here rather than inside the rebuild so the panel is already dark
         * before the triggering press-release even finishes drawing. The
         * rebuild arranges for it to come back. */
        blank_backlight();
        lv_async_call(rebuild_screens_for_theme_change, nullptr);
    }

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

        request_rebuild(RebuildTarget::Settings);
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
        remote_last_settings() = app.config_store.get().settings;
        remote_shown_mode()    = remote_last_settings().brightness_mode;
        app.wireless_setup_screen.open(app.remote_binding.build_view_model(),
                                       app.settings_screen.screen());
    }

    /* Fired by RemoteConfigServer whenever a phone-side request changes the
     * live config: a preview (unsaved), a Save, or the end of the phone's
     * preview session. In
     * every case the config store already holds the values to show; this
     * brings the panel in line with them, so what the phone is editing is
     * on the dashboard as it's edited.
     *
     * Brightness follows whichever of day/night/auto the phone last
     * dragged, so dragging the night slider is visible on the panel, and
     * otherwise the mode the phone picked. A Save or a discard goes back to the
     * saved mode.
     *
     * Orientation is NOT applied and cannot be -- it is boot-time-only
     * (see platform.hpp's DisplayOrientation). The phone's own mock shows
     * it; the panel rotates on the next restart. Unlike a local layout
     * change this doesn't restart on its own, since that would drop the
     * phone's session mid-edit. The 180-degree flip is applied, since it
     * can be: the page has no control for it, but a config loaded from a
     * file carries one, and a discard can put it back. */
    FLASHMEM void remote_config_event_cb(
        void *ctx, remote::server::RemoteConfigServer::ConfigEvent event)
    {
        (void)ctx;
        using Event = remote::server::RemoteConfigServer::ConfigEvent;

        Composition          &app      = composition();
        const core::Settings &settings = app.config_store.get().settings;
        uint32_t              now      = platform_get().clock.millis();
        app.remote_ap.note_activity(now);
        remote_last_request_ms() = now;

        core::BrightnessMode &shown = remote_shown_mode();
        if (event == Event::Previewed)
        {
            const core::Settings &last = remote_last_settings();
            if (settings.night_brightness != last.night_brightness)
            {
                shown = core::BrightnessMode::Night;
            }
            else if (settings.day_brightness != last.day_brightness)
            {
                shown = core::BrightnessMode::Day;
            }
            else if (settings.auto_brightness_pct != last.auto_brightness_pct)
            {
                shown = core::BrightnessMode::Auto;
            }
            else if (settings.brightness_mode != last.brightness_mode)
            {
                shown = settings.brightness_mode;
            }
        }
        else
        {
            shown = settings.brightness_mode;
        }
        remote_last_settings() = settings;
        set_live_brightness_mode(shown, mode_slider_pct(settings, shown));

        bool flipped = flipped_for(app.config_store.get().settings,
                                   active_orientation());
        if (flipped != panel_flipped())
        {
            apply_panel_flip(flipped);
        }

        /* A remote change can rewrite priorities[] and num_panels
         * wholesale, so the poller's request table has to follow it. */
        sync_active_signals();

        /* The phone's mock shows sample values; the panel shows the same
         * ones for as long as the phone is driving it. */
        app.vehicle_binding.set_value_override(
            app.remote_server.preview_values());

        /* Kept in step even mid-preview: SettingsScreen commits its own
         * copy of these on every exit, which would otherwise write stale
         * values back over whatever the phone just did. */
        app.settings_screen.set_values(
            settings.day_brightness,
            settings.night_brightness,
            settings.auto_brightness_pct,
            gui::to_gui_brightness_mode(settings.brightness_mode),
            settings.num_panels,
                                       settings.accent_color);

        /* The phone's own edits (and its Discard) land on the dashboard,
         * which is what they change. A session that ended because the
         * phone went -- its page closed, it left the AP, it fell silent --
         * goes back to the QR code, ready for it to come back: the
         * dashboard it was driving would otherwise just drop to live
         * values with no sign of why. The panel's X ends it too, but stays
         * put: it's already on its way back to Settings. */
        bool phone_gone = reverting_for_lost_phone()
                          || event == Event::PageLeft;
        bool to_dashboard
            = !phone_gone
              && (!reverting_locally()
                  || lv_screen_active() == app.info_screen.screen());
        RebuildTarget target = phone_gone     ? RebuildTarget::Wireless
                               : to_dashboard ? RebuildTarget::Dashboard
                                              : RebuildTarget::Settings;

        app.info_screen.set_preview_banner(
            app.remote_server.preview_active());

        if (settings.accent_color != built_accent())
        {
            request_rebuild(target);
            return;
        }

        app.info_screen.rebuild(settings.num_panels,
                                to_gui_orientation(active_orientation()));
        app.info_screen.refresh();
        if (target == RebuildTarget::Wireless
            && lv_screen_active() != app.wireless_setup_screen.screen())
        {
            app.wireless_setup_screen.open(
                app.remote_binding.build_view_model(),
                app.settings_screen.screen());
        }
        else if (to_dashboard)
        {
            lv_screen_load(app.info_screen.screen());
        }
    }

    FLASHMEM void wireless_deactivate_cb(void *ctx)
    {
        (void)ctx;
        composition().remote_binding.deactivate();
        revert_remote_preview();
        sync_wifi_indicator();
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

    FLASHMEM void firmware_install_cb(void *ctx);
    FLASHMEM void firmware_leave_cb(void *ctx);

    /* Takes the update screen down -- it exists only in update mode (see
     * firmware_update_screen.hpp for what it cost to keep). If it's
     * showing, back to the Wi-Fi screen the phone is connected over. */
    FLASHMEM void leave_firmware_update()
    {
        Composition &app = composition();
        if (lv_screen_active() == app.firmware_update_screen.screen())
        {
            app.wireless_setup_screen.open(
                app.remote_binding.build_view_model(),
                app.settings_screen.screen());
        }
        app.firmware_update_screen.destroy();
    }

    FLASHMEM void open_firmware_update_screen()
    {
        Composition &app = composition();
        app.firmware_update_screen.create(runtime_theme(),
                                          firmware_install_cb,
                                          nullptr,
                                          firmware_leave_cb,
                                          nullptr);
        app.firmware_update_screen.open(
            app.remote_binding.build_update_view_model());
    }

    /* Fired by the Install popup. The relay switches app slots and
     * restarts the board; the screen says so in the meantime. */
    FLASHMEM void firmware_install_cb(void *ctx)
    {
        (void)ctx;
        Composition &app = composition();
        app.remote_ota.install(platform_get().clock.millis());
        app.firmware_update_screen.refresh(
            app.remote_binding.build_update_view_model());
    }

    /* Fired by the update screen's X and the Install popup's Cancel: out
     * of update mode. A verified image stays unused in the spare slot
     * until the next upload overwrites it. */
    FLASHMEM void firmware_leave_cb(void *ctx)
    {
        (void)ctx;
        composition().remote_ota.leave();
        leave_firmware_update();
    }

    /*------------------------------------------------------------------
     * The "Update firmware?" popup -- the phone asking for update mode,
     * put to whoever is in the car. Same shape as the sniffer's request
     * (request_sniff()): Cancel/Start, and a countdown ring for the
     * automatic decline (OTA_REQUEST_TIMEOUT_MS).
     *-----------------------------------------------------------------*/

    lv_obj_t *s_ota_request_popup = nullptr;
    lv_obj_t *s_ota_request_ring  = nullptr; /* the popup's countdown */

    FLASHMEM void close_update_request_popup()
    {
        if (s_ota_request_popup != nullptr)
        {
            lv_msgbox_close_async(s_ota_request_popup);
            s_ota_request_popup = nullptr;
            s_ota_request_ring  = nullptr; /* goes with it */
        }
    }

    FLASHMEM void update_request_start_cb(lv_event_t *e)
    {
        (void)e;
        close_update_request_popup();
        composition().remote_ota.accept(platform_get().clock.millis());
        open_firmware_update_screen();
    }

    FLASHMEM void update_request_cancel_cb(lv_event_t *e)
    {
        (void)e;
        close_update_request_popup();
        composition().remote_ota.decline();
    }

    FLASHMEM void open_update_request_popup()
    {
        gui::ConfirmPopup popup = gui::create_confirm_popup(
            runtime_theme(), "Update firmware?", NULL, "Start");
        lv_obj_add_event_cb(popup.confirm_btn,
                            update_request_start_cb,
                            LV_EVENT_CLICKED,
                            nullptr);
        lv_obj_add_event_cb(popup.cancel_btn,
                            update_request_cancel_cb,
                            LV_EVENT_CLICKED,
                            nullptr);
        s_ota_request_popup = popup.mbox;
        s_ota_request_ring  = gui::add_popup_countdown(
            popup,
            runtime_theme(),
            remote::server::OTA_REQUEST_TIMEOUT_MS / 100);
    }

    /* Follows RemoteOtaController: the request popup while the phone is
     * asking, then the update screen for as long as update mode lasts --
     * put back up if anything else loads over it -- and back to the Wi-Fi
     * screen when it ends. Also keeps the AP's idle timeout from cutting
     * off update mode underneath it; the X is the way out, or
     * remote::server::OTA_WAIT_TIMEOUT_MS if nothing is ever sent. */
    FLASHMEM void firmware_update_task(void *ctx)
    {
        (void)ctx;
        Composition &app = composition();
        uint32_t     now = platform_get().clock.millis();
        app.remote_ota.tick(now);

        if (app.remote_ota.state() == remote::server::OtaState::Requested)
        {
            if (s_ota_request_popup == nullptr)
            {
                open_update_request_popup();
            }
            else
            {
                /* Tenths of a second, like the sniffer's ring. */
                uint32_t waited = now - app.remote_ota.state_entered_ms();
                uint32_t left
                    = waited < remote::server::OTA_REQUEST_TIMEOUT_MS
                          ? remote::server::OTA_REQUEST_TIMEOUT_MS - waited
                          : 0;
                lv_arc_set_value(s_ota_request_ring, left / 100);
            }
        }
        else
        {
            /* Answered, or timed out (tick() just declined it). */
            close_update_request_popup();
        }

        lv_obj_t *screen  = app.firmware_update_screen.screen();
        bool      showing = screen != nullptr && lv_screen_active() == screen;
        if (!app.remote_ota.in_update_mode())
        {
            if (screen != nullptr)
            {
                leave_firmware_update();
            }
            return;
        }

        app.remote_ap.note_activity(now);
        if (showing)
        {
            app.firmware_update_screen.refresh(
                app.remote_binding.build_update_view_model());
        }
        else
        {
            open_firmware_update_screen();
        }
    }

    /* Ends a new firmware's probation (platform_firmware_healthy()) once
     * it has run FIRMWARE_HEALTHY_AFTER_MS -- this task's first run --
     * with frames reaching the panel and the relay that serves the update
     * page up, so the next update can get in. Until then any reset, the
     * hang watchdog's included, rolls the update back. Runs on after
     * that, but the platform call does nothing a second time. */
    FLASHMEM void firmware_health_task(void *ctx)
    {
        (void)ctx;
        if (last_flush_ms() != 0 && platform_update_path_ready())
        {
            platform_firmware_healthy();
        }
    }

    FLASHMEM void close_notice_cb(lv_event_t *e)
    {
        lv_msgbox_close_async(
            static_cast<lv_obj_t *>(lv_event_get_user_data(e)));
    }

    /* The board came back up on its old firmware after an update. Said
     * once, over whatever screen boot lands on. */
    FLASHMEM void show_failed_update_notice()
    {
        char failed[FIRMWARE_VERSION_LEN];
        if (!platform_take_failed_update(failed, sizeof(failed)))
        {
            return;
        }

        char body[16 + FIRMWARE_VERSION_LEN];
        snprintf(
            body, sizeof(body), "Still on %s", platform_firmware_version());

        gui::ConfirmPopup popup = gui::create_confirm_popup(
            runtime_theme(), "Update failed", body, "OK");
        lv_obj_set_hidden(popup.cancel_btn, true);
        lv_obj_add_event_cb(
            popup.confirm_btn, close_notice_cb, LV_EVENT_CLICKED, popup.mbox);
    }

    /* A tap on the dashboard that isn't on a panel: the gaps, and the
     * preview banner's strip. Only means something mid-preview, where it
     * is what the banner promises. */
    FLASHMEM void info_screen_clicked_cb(lv_event_t *e)
    {
        (void)e;
        /* A swipe that started off a panel -- see
         * InfoScreen::panel_clicked_cb. */
        if (lv_indev_get_gesture_dir(lv_indev_active()) != LV_DIR_NONE)
        {
            return;
        }
        divert_to_wireless_if_previewing();
    }

    void info_screen_gesture_cb(lv_event_t *e)
    {
        if (lv_event_get_code(e) != LV_EVENT_GESTURE)
        {
            return;
        }

        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

        if ((dir == LV_DIR_BOTTOM || dir == LV_DIR_TOP)
            && divert_to_wireless_if_previewing())
        {
            return;
        }

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

        built_accent() = settings.accent_color;

        app.info_screen.init(theme,
                             view_model_builder,
                             &app.vehicle_binding,
                             panel_pressed_cb,
                             nullptr);
        app.info_screen.set_logging_active(settings.logging_enabled
                                           && app.logger.is_ready());
        app.info_screen.set_preview_banner(app.remote_server.preview_active());
        app.configurator_screen.init(theme,
                                     app.signal_names,
                                     app.signal_ids,
                                     app.signal_is_state,
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
                                 platform_firmware_version(),
                                 settings.day_brightness,
                                 settings.night_brightness,
                                 settings.auto_brightness_pct,
                                 gui::to_gui_brightness_mode(
                                     settings.brightness_mode),
                                 settings.num_panels,
                                 settings.accent_color,
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
                                 settings_pick_orientation_cb,
                                 nullptr);
        app.logging_screen.init(
            theme, logging_toggle_cb, nullptr, logging_erase_cb, nullptr);
        app.theme_picker_screen.init(theme, theme_picker_done_cb, nullptr);
        app.orientation_screen.init(theme,
                                    orientation_layout_confirm_cb,
                                    nullptr,
                                    orientation_rotate_cb,
                                    nullptr);
        app.wireless_setup_screen.init(theme, wireless_deactivate_cb, nullptr);
        app.wifi_indicator.init(theme);
        sync_wifi_indicator();

        lv_obj_add_event_cb(app.info_screen.screen(),
                            info_screen_gesture_cb,
                            LV_EVENT_GESTURE,
                            nullptr);
        lv_obj_add_event_cb(app.info_screen.screen(),
                            info_screen_clicked_cb,
                            LV_EVENT_CLICKED,
                            nullptr);
        lv_obj_add_event_cb(app.settings_screen.screen(),
                            settings_screen_loaded_cb,
                            LV_EVENT_SCREEN_LOADED,
                            nullptr);

        /* active_orientation(), not settings.orientation -- this runs
         * again from rebuild_screens_for_theme_change() mid-session, by
         * which point settings.orientation may already hold a phone's
         * staged pick that hasn't taken effect yet (see
         * active_orientation()). */
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
        lv_obj_delete(app.orientation_screen.screen());
        lv_obj_delete(app.wireless_setup_screen.screen());
        /* Not built by build_screens(): firmware_update_task builds it
         * again against the new theme if update mode is still on. */
        app.firmware_update_screen.destroy();
        app.wifi_indicator.destroy();

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

        rebuild_pending() = false;

        lv_obj_t *transition = destroy_screens();
        build_screens();

        if (rebuild_target() == RebuildTarget::Dashboard)
        {
            /* The dashboard has no load animation to wait out, and never
             * goes quiet (its readouts refresh constantly), so it settles
             * the way boot does: by counting render passes. */
            lv_screen_load(composition().info_screen.screen());
            boot_passes()     = 0;
            backlight_state() = BacklightState::AwaitingBootPasses;
        }
        else if (rebuild_target() == RebuildTarget::Wireless)
        {
            Composition &app = composition();
            app.wireless_setup_screen.open(
                app.remote_binding.build_view_model(),
                app.settings_screen.screen());
            /* A still screen once its load animation ends, so the same
             * quiet-period settle as Settings. Guarded for STRESS_TEST
             * builds, where blank_backlight() leaves it lit. */
            if (backlight_state() == BacklightState::Blanked)
            {
                backlight_state() = BacklightState::Settling;
            }
        }
        else
        {
            /* settings_screen_loaded_cb brings the light back. */
            composition().settings_screen.open();
        }
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

    /* The saved level from the first lit frame -- see backlight_pct(). */
    /* Auto starts from Day's level, its fallback, and snaps to the
     * dimmer's before the panel lights if the cluster answers in time. */
    live_brightness_mode() = settings.brightness_mode;
    live_auto_pct()        = settings.auto_brightness_pct;
    backlight_pct() = settings.brightness_mode == core::BrightnessMode::Night
                          ? settings.night_brightness
                          : settings.day_brightness;
    auto_brightness().start(backlight_pct(), platform.clock.millis());

    DisplayOrientation orientation
        = to_display_orientation(to_gui_orientation(settings.orientation));

    platform.display.begin();
    bool flipped    = flipped_for(settings, settings.orientation);
    panel_flipped() = flipped;
    platform.display.set_orientation(orientation, flipped);
    platform.touch.begin();
    platform.touch.set_orientation(orientation, flipped);
    uint32_t can_rx_lo;
    uint32_t can_rx_hi;
    core::uds_ecu_rx_range(can_rx_lo, can_rx_hi);
    platform.can.begin(can_rx_lo, can_rx_hi, CAN_BAUD);
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

    app.remote_server.set_config_callback(remote_config_event_cb, nullptr);

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
    app.scheduler.every(
        AUTO_BRIGHTNESS_PERIOD_MS, auto_brightness_task, nullptr);
    app.scheduler.every(remote::server::AP_IDLE_CHECK_PERIOD_MS,
                        remote_ap_idle_check_task,
                        nullptr);
    app.scheduler.every(
        WIRELESS_SCREEN_REFRESH_MS, refresh_wireless_screen_task, nullptr);
    app.scheduler.every(LOG_SAMPLE_PERIOD_MS, logging_sample_task, nullptr);
    app.scheduler.every(
        LOGGING_SCREEN_REFRESH_MS, refresh_logging_screen_task, nullptr);
    app.scheduler.every(
        FIRMWARE_UPDATE_REFRESH_MS, firmware_update_task, nullptr);
    app.scheduler.every(
        FIRMWARE_HEALTHY_AFTER_MS, firmware_health_task, nullptr);

    /* Arms the backstop for the boot blank. The panel is already dark
     * (TeensyDisplay::begin() holds the pin low) and display_flush_cb's
     * pass counting is what normally lights it -- this is purely so a
     * boot where those passes never arrive still ends with a lit
     * dashboard rather than a black one. Has to come after lv_init(),
     * since it creates an lv_timer. */
    begin_backlight_wait();

    if (settings.open_orientation_on_boot)
    {
        /* Cleared first, so a crash on the way up can't make every later
         * boot land here too. */
        settings.open_orientation_on_boot = false;
        app.config_store.save();

        app.orientation_screen.open(to_gui_orientation(active_orientation()),
                                    app.settings_screen.screen());

        /* A static screen never produces the BOOT_SETTLE_PASSES the
         * dashboard does, so it settles like orientation_rotate_cb: draw
         * it now, then light once the display goes quiet. */
        lv_refr_now(nullptr);
        backlight_state() = BacklightState::Settling;
    }
    else
    {
        lv_screen_load(app.info_screen.screen());
    }

    show_failed_update_notice();

    /* Last, so nothing above -- the first LittleFS mount after a
     * partition change can format it -- runs against it. From here on a
     * loop() that stops coming round resets the board. */
    platform_hang_watchdog(true);
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
