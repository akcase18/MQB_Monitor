/**
 * @file main.cpp
 * @brief Composition root. Builds the target board's Platform and (in
 * later phases) wires up core/gui objects on top of it. This is the only
 * file that references a concrete platform_get() implementation, and only
 * one implementation is ever linked per build (see platformio.ini).
 *
 * Phase 5 smoke test: bridges IDisplay to a minimal LVGL display driver
 * and renders a static grid of gui::PanelWidgets built from fabricated
 * PanelViewModels, to visually confirm gui/theme + gui/widgets render
 * correctly before core/gui are wired together for real in Phase 7.
 */

#include "panel_widget.hpp"
#include "platform.hpp"
#include "theme.hpp"

#include <lvgl.h>
#include <stdio.h>

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace
{
constexpr uint16_t LCD_HORZ = 480;
constexpr uint16_t LCD_VERT = 320;

constexpr uint8_t NUM_DEMO_PANELS = 5;

struct DemoPanel
{
        const char *name;
        const char *unit;
        const char *value;
        bool        large;
        bool        warn_lo;
        bool        warn_hi;
};

constexpr DemoPanel DEMO_PANELS[NUM_DEMO_PANELS] = {
    { "Coolant Temp",     "F",   "192", false, false, false },
    { "Misfire Cnt.",     "",    "0",   false, false, false },
    { "Intake Air Temp",  "F",   "88",  false, false, true  },
    { "Intake Pressure",  "psi", "14",  true,  true,  false },
    { "Eng Oil Temp",     "F",   "210", true,  false, false },
};

lv_color_t s_draw_buf_1[(LCD_HORZ * LCD_VERT) / 10];
lv_color_t s_draw_buf_2[(LCD_HORZ * LCD_VERT) / 10];

gui::PanelWidget s_panels[NUM_DEMO_PANELS];

void display_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    platform_get().display.flush(
        area->x1, area->y1, area->x2, area->y2,
        reinterpret_cast<const uint16_t *>(px_map));
    lv_display_flush_ready(disp);
}

uint32_t tick_cb()
{
    return platform_get().clock.millis();
}

/**
 * Sets up a non-wrapping flex row so its children's flex_grow(1) (set by
 * PanelWidget::init) divides the row's width evenly between them. A single
 * LV_FLEX_FLOW_ROW_WRAP container with grow-1 children doesn't work here —
 * LVGL decides wrapping from each child's pre-grow (near-zero) natural
 * width, so all children fit on one line and only *then* grow to fill it,
 * overflowing instead of wrapping.
 */
lv_obj_t *create_panel_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, gui::THEME_DEFAULT.metrics.panel_gap, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    return row;
}

void build_demo_panels(lv_obj_t *top_row, lv_obj_t *bottom_row)
{
    for (uint8_t i = 0; i < NUM_DEMO_PANELS; i++)
    {
        const DemoPanel &demo = DEMO_PANELS[i];

        s_panels[i].init(demo.large ? bottom_row : top_row, gui::THEME_DEFAULT);

        gui::PanelViewModel vm{};
        snprintf(vm.name, sizeof(vm.name), "%s", demo.name);
        snprintf(vm.unit_label, sizeof(vm.unit_label), "%s", demo.unit);
        snprintf(vm.value_str, sizeof(vm.value_str), "%s", demo.value);
        vm.large          = demo.large;
        vm.warn_lo_active = demo.warn_lo;
        vm.warn_hi_active = demo.warn_hi;

        s_panels[i].set_view_model(vm);
    }
}
} // namespace

void setup()
{
    Platform &platform = platform_get();

    platform.display.begin();
    platform.touch.begin();

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

    lv_obj_t *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, gui::THEME_DEFAULT.palette.bg, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_pad_row(root, 0, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);

    /* Small panels on top, large panels on bottom — matches the old
     * Layout's 3-small/2-large split, and its panel_height_sm_pct (0.45)
     * + panel_height_lg_pct (0.55) sum to exactly 1 screen height, so the
     * two rows fill the screen with no gap between them. */
    lv_obj_t *top_row    = create_panel_row(root);
    lv_obj_t *bottom_row = create_panel_row(root);

    build_demo_panels(top_row, bottom_row);
}

void loop()
{
    Platform &platform = platform_get();

    /* Also pumps the simulator's SDL event queue (window close, etc). */
    TouchPoint point{};
    platform.touch.read(point);

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
