#include "gui.hpp"

/*************************
 * Panel Object Creation *
 *************************/

Panel panel_tl = Panel(&coolant_temp, btn_tl, data_tl, label_tl);      // Top left Panel object
Panel panel_tc = Panel(&engine_oil_temp, btn_tc, data_tc, label_tc);   // Top center Panel object
Panel panel_tr = Panel(&intake_air_temp, btn_tr, data_tr, label_tr);   // Top right Panel object
Panel panel_bl = Panel(&intake_pressure, btn_bl, data_bl, label_bl);   // Bottom left Panel object
Panel panel_br = Panel(&throttle_position, btn_br, data_br, label_br); // Bottom right Panel object

/**************************
 * Layout Object Creation *
 **************************/

Layout layout_1 = Layout(&panel_tl, &panel_tc, &panel_tr, &panel_bl, &panel_br); // Main layout creation

/*************************
 * Configurator Creation *
 *************************/

Configurator config = Configurator();

/*************
 * Callbacks *
 *************/

/**
 * Callback for opening the Configurator window when a Panel button
 * is pressed.
 */
void open_config_cb(lv_event_t *e)
{
    Panel *panel_sel = (Panel *)lv_event_get_user_data(e); // Get the user data passed into callback
    config.panel_selected = panel_sel;                     // Keep track of the Panel that was pressed to open the Configurator
    config.show_config_window();
}

/**
 * Callback for the "done" button in the Configurator window to
 * close the Configurator window.
 */
void close_config_cb(lv_event_t *e)
{
    config.close_config_window();
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
    panel_tl.set_styles(&style_btn_top, &style_label_top);
    /* Top Center Panel */
    panel_tc.set_styles(&style_btn_top, &style_label_top);
    /* Top Right Panel */
    panel_tr.set_styles(&style_btn_top, &style_label_top);
    /* Bottom Left Panel */
    panel_bl.set_styles(&style_btn_btm, &style_label_btm);
    /* Bottom Right Panel */
    panel_br.set_styles(&style_btn_btm, &style_label_btm);
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

void setup()
{
    style_init();
    add_style_to_panels();
    setup_callbacks();
}

void loop()
{
}