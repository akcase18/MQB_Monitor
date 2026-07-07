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
#include "scheduler.hpp"
#include "signal_catalog.hpp"
#include "signal_store.hpp"
#include "uds_client.hpp"

#include "configurator_screen.hpp"
#include "info_screen.hpp"
#include "settings_screen.hpp"
#include "signal_picker_screen.hpp"
#include "theme_picker_screen.hpp"
#include "vehicle_binding.hpp"
#include "warning_config_screen.hpp"

#include <lvgl.h>
#include <stdint.h>

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

    constexpr uint32_t INFO_SCREEN_REFRESH_MS = 200;

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

            gui::VehicleBinding vehicle_binding;

            gui::InfoScreen          info_screen;
            gui::ConfiguratorScreen  configurator_screen;
            gui::SignalPickerScreen  signal_picker_screen;
            gui::WarningConfigScreen warning_screen;
            gui::SettingsScreen      settings_screen;
            gui::ThemePickerScreen   theme_picker_screen;

            const char *signal_names[core::SIGNAL_CATALOG_COUNT];
            uint16_t    signal_ids[core::SIGNAL_CATALOG_COUNT];

            explicit Composition(Platform &platform)
                : config_store(platform.config_store)
                , uds_client(platform.can, CAN_TX_ID, CAN_RX_ID)
                , scheduler(platform.clock)
                , vehicle_binding(signal_store, config_store)
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

    gui::LayoutOrientation to_gui_orientation(
        core::LayoutOrientation orientation)
    {
        return orientation == core::LayoutOrientation::Vertical
                   ? gui::LayoutOrientation::Vertical
                   : gui::LayoutOrientation::Horizontal;
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

    void on_uds_response(void *ctx, uint8_t byte1, uint8_t byte2, float raw)
    {
        (void)ctx;
        composition().signal_store.on_uds_response(
            byte1, byte2, raw, platform_get().clock.millis());
    }

    gui::PanelViewModel view_model_builder(void   *ctx,
                                           uint8_t panel_idx,
                                           bool    large,
                                           bool    hero)
    {
        auto *binding = static_cast<gui::VehicleBinding *>(ctx);
        return binding->build_view_model(panel_idx, large, hero);
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

    void panel_pressed_cb(void *ctx, uint8_t panel_idx)
    {
        (void)ctx;
        const core::Config      &cfg   = composition().config_store.get();
        const core::PanelConfig &panel = cfg.panels[panel_idx];

        composition().configurator_screen.open(
            panel_idx,
            panel.signal_id,
            panel.units == core::UnitSystem::Metric,
            to_gui_display_mode(panel.display_mode));
    }

    void configurator_done_cb(void            *ctx,
                              uint8_t          panel_idx,
                              uint16_t         signal_id,
                              bool             is_metric,
                              gui::DisplayMode mode)
    {
        (void)ctx;
        composition().vehicle_binding.apply_configurator_change(
            panel_idx,
            signal_id,
            is_metric ? core::UnitSystem::Metric : core::UnitSystem::Imperial,
            mode);
        composition().info_screen.refresh();
        lv_screen_load(composition().info_screen.screen());
    }

    void configurator_settings_cb(void    *ctx,
                                  uint8_t  panel_idx,
                                  uint16_t signal_id)
    {
        (void)ctx;

        const core::Config      &cfg   = composition().config_store.get();
        const core::PanelConfig &panel = cfg.panels[panel_idx];
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
        platform_get().display.set_backlight(day_brightness_pct);

        core::Settings &settings  = composition().config_store.get().settings;
        settings.day_brightness   = day_brightness_pct;
        settings.night_brightness = night_brightness_pct;
        settings.num_panels       = num_panels;
        composition().config_store.save();

        composition().info_screen.rebuild(
            num_panels, to_gui_orientation(settings.orientation));
        lv_screen_load(composition().info_screen.screen());
    }

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
                                 settings_done_cb,
                                 nullptr,
                                 settings_preview_brightness_cb,
                                 nullptr,
                                 settings_pick_theme_cb,
                                 nullptr);
        app.theme_picker_screen.init(theme, theme_picker_done_cb, nullptr);

        lv_obj_add_event_cb(app.info_screen.screen(),
                            info_screen_gesture_cb,
                            LV_EVENT_GESTURE,
                            nullptr);

        app.info_screen.rebuild(settings.num_panels,
                                to_gui_orientation(settings.orientation));
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

    platform.display.begin();
    platform.touch.begin();
    platform.can.begin(CAN_TX_ID, CAN_RX_ID, CAN_BAUD);

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

    Composition &app = composition();
    app.config_store.load();

    /* Guard against an accent_color that isn't one of gui::ACCENT_PRESETS
     * (e.g. 0 from a config saved before this field existed) — silently
     * building every screen against black-on-black accent styling is a
     * much worse failure mode than just falling back to a real default,
     * and this also self-heals the saved value so it doesn't recur. */
    core::Settings &settings     = app.config_store.get().settings;
    bool            valid_accent = false;
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

    /* Seed runtime_theme()'s accent from whatever was last saved, before
     * the first build_screens() call — otherwise every screen would
     * briefly be built against THEME_DEFAULT's hardcoded green instead of
     * the user's actual saved preference. */
    runtime_theme().palette.accent = lv_color_hex(settings.accent_color);
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

    lv_screen_load(app.info_screen.screen());
}

void loop()
{
    Composition &app = composition();

    app.scheduler.tick();
    app.uds_client.poll(on_uds_response, nullptr);

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
