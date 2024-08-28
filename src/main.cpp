#include "gui.hpp"
#include "ILI948x_t4_mm.h"

/********************
 * Global Constants *
 ********************/

#define PWM_PIN 3
#define MAX_BRIGHTNESS 256

/***********************
 * Display Constructor *
 ***********************/

#define LCD_CS 4
#define LCD_DC 5
#define LCD_RST 26
DMAMEM ILI948x_t4_mm lcd = ILI948x_t4_mm(LCD_DC, LCD_CS, LCD_RST);
lv_display_t *display;

/*************
 * Callbacks *
 *************/

/**
 * Callback for opening the Configurator window when a Panel button
 * is pressed.
 */
void open_config_cb(lv_event_t *e)
{
    layout.close_window();
    Panel *panel_sel = (Panel *)lv_event_get_user_data(e); // Get the user data passed into callback
    config.panel_selected = panel_sel;                     // Keep track of the Panel that was pressed to open the Configurator
    config.open_window();
}

/**
 * Callback for the "done" button in the Configurator window to
 * close the Configurator window.
 */
void close_config_cb(lv_event_t *e)
{
    config.close_window();
    layout.open_window();
}

/**
 * Callback to service the radio buttons in the Configurator window.
 */
void radio_cb(lv_event_t *e)
{
    // TODO: Fill out
}

/**
 * Add button and label styles to the Panels
 */
void add_style_to_panels()
{
    /* Top Left Panel */
    panel_tl.set_styles(&style_btn_top, &style_btn_pressed, &style_label_top);
    /* Top Center Panel */
    panel_tc.set_styles(&style_btn_top, &style_btn_pressed, &style_label_top);
    /* Top Right Panel */
    panel_tr.set_styles(&style_btn_top, &style_btn_pressed, &style_label_top);
    /* Bottom Left Panel */
    panel_bl.set_styles(&style_btn_btm, &style_btn_pressed, &style_label_btm);
    /* Bottom Right Panel */
    panel_br.set_styles(&style_btn_btm, &style_btn_pressed, &style_label_btm);
}

/**
 * Add callbacks to all of the necessary objects, buttons, windows, etc.
 */
void setup_callbacks()
{
    /* Panels */
    panel_tl.set_callback(panel_tl.btn, open_config_cb, LV_EVENT_CLICKED, &panel_tl);
    panel_tc.set_callback(panel_tc.btn, open_config_cb, LV_EVENT_CLICKED, &panel_tc);
    panel_tr.set_callback(panel_tr.btn, open_config_cb, LV_EVENT_CLICKED, &panel_tr);
    panel_bl.set_callback(panel_bl.btn, open_config_cb, LV_EVENT_CLICKED, &panel_bl);
    panel_br.set_callback(panel_br.btn, open_config_cb, LV_EVENT_CLICKED, &panel_br);
    /* Configurator */
    config.set_callback(config.btn_done, close_config_cb, LV_EVENT_CLICKED, NULL); // Close Configurator when "Done" button is pressed
    config.set_callback(config.flex_col_2, radio_cb, LV_EVENT_CLICKED, NULL);      // Trigger/Switch radio button selection when pressed
}

/**
 * Add Information objects to the Panels. This will eventually
 * need to pull data from some sort of config file, so changes
 * will save when the display turns off.
 */
void info_init()
{
    // TODO: Change this to use EEPROM or other setup file data
    /* Top Left Panel */
    panel_tl.panel_info = &coolant_temp;
    panel_tl.panel_info->set_is_metric(false);
    /* Top Center Panel */
    panel_tc.panel_info = &engine_oil_temp;
    panel_tc.panel_info->set_is_metric(false);
    /* Top Right Panel */
    panel_tr.panel_info = &intake_air_temp;
    panel_tr.panel_info->set_is_metric(false);
    /* Bottom Left Panel */
    panel_bl.panel_info = &intake_pressure;
    panel_bl.panel_info->set_is_metric(false);
    /* Bottom Right Panel */
    panel_br.panel_info = &throttle_position;
    panel_br.panel_info->set_is_metric(false);
}

void set_brightness(int percent)
{
    if (percent > 100)
    {
        percent = 100;
    }
    else if (percent < 0)
    {
        percent = 0;
    }
    pinMode(PWM_PIN, OUTPUT);                  // Set the brightness pin to an output pin
    int brightness = percent * MAX_BRIGHTNESS; // Get the brightness value
    analogWrite(PWM_PIN, brightness);          // Set the brightness value
}

void display_flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
    lcd.pushPixels16bitDMA((uint16_t *)data, area->x1, area->y1, area->x2, area->y2);
    lv_display_flush_ready(display);
}

void display_init()
{
    set_brightness(100);
    display = lv_display_create(LCD_HORZ, LCD_VERT);
    lv_display_set_flush_cb(display, display_flush);
    lv_display_set_buffers(display, disp_buf_1, disp_buf_2, sizeof(disp_buf_1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void setup()
{
    lv_init();

    display_init();

    lv_task_handler();

    delay(1000);

    can_init();
    style_init();
    info_init();
    add_style_to_panels();
    setup_callbacks();

    layout.open_window();
}

void loop()
{
    lv_task_handler();
}