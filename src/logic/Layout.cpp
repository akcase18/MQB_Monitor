#include "Layout.hpp"

#define LED 1

/**************************
 * Layout Font Definition *
 **************************/

const lv_font_t *top_data_font = &digital_display_100;
const lv_font_t *btm_data_font = &digital_display_140;
const lv_font_t *top_label_font = &vw_16;
const lv_font_t *btm_label_font = &vw_18;

/****************************
 * Layout Object Definition *
 ****************************/

DMAMEM Layout layout = Layout(&panel_tl, &panel_tc, &panel_tr, &panel_bl, &panel_br);

/****************
 * Constructors *
 ****************/

Layout::Layout()
{
    // Please don't use
}

Layout::Layout(Panel *top_left, Panel *top_center, Panel *top_right, Panel *bottom_left, Panel *bottom_right)
{
    panel_tl = top_left;
    panel_tc = top_center;
    panel_tr = top_right;
    panel_bl = bottom_left;
    panel_br = bottom_right;
}

/***************************
 * Public Helper Functions *
 ***************************/

void Layout::init_layout()
{
    style_init();
    init_window();
    init_objects();
}

void Layout::style_init()
{
    lv_style_init(&style_window);
    lv_style_set_bg_color(&style_window, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_border_width(&style_window, 0);
    lv_style_set_radius(&style_window, 0);

    panel_style_init();
}

void Layout::panel_style_init()
{
    /***** Top Panels *****/
    /* Buttons */
    lv_style_init(&style_btn_top);
    lv_style_set_radius(&style_btn_top, 2);
    lv_style_set_opa(&style_btn_top, LV_OPA_100);
    lv_style_set_bg_color(&style_btn_top, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_text_color(&style_btn_top, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_text_font(&style_btn_top, top_data_font);
    lv_style_set_text_letter_space(&style_btn_top, 0);
    lv_style_set_shadow_width(&style_btn_top, 0);
    /* Labels */
    lv_style_init(&style_label_top);
    lv_style_set_text_color(&style_label_top, lv_palette_lighten(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(&style_label_top, top_label_font);
    lv_style_set_text_letter_space(&style_label_top, 0);
    /***** Bottom Panels *****/
    /* Buttons */
    lv_style_init(&style_btn_btm);
    lv_style_set_radius(&style_btn_btm, 2);
    lv_style_set_opa(&style_btn_btm, LV_OPA_100);
    lv_style_set_bg_color(&style_btn_btm, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_text_color(&style_btn_btm, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_text_font(&style_btn_btm, btm_data_font);
    lv_style_set_text_letter_space(&style_btn_btm, 0);
    lv_style_set_shadow_width(&style_btn_btm, 0);
    /* Labels */
    lv_style_init(&style_label_btm);
    lv_style_set_text_color(&style_label_btm, lv_palette_lighten(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(&style_label_btm, btm_label_font);
    lv_style_set_text_letter_space(&style_label_btm, 0);

    /***** Top Left Panel *****/
    panel_tl->style_btn = &style_btn_top;
    panel_tl->style_btn_pressed = &style_btn_pressed;
    panel_tl->style_label = &style_label_top;
    /***** Top Center Panel *****/
    panel_tc->style_btn = &style_btn_top;
    panel_tc->style_btn_pressed = &style_btn_pressed;
    panel_tc->style_label = &style_label_top;
    /***** Top Right Panel *****/
    panel_tr->style_btn = &style_btn_top;
    panel_tr->style_btn_pressed = &style_btn_pressed;
    panel_tr->style_label = &style_label_top;
    /***** Bottom Left Panel *****/
    panel_bl->style_btn = &style_btn_btm;
    panel_bl->style_btn_pressed = &style_btn_pressed;
    panel_bl->style_label = &style_label_btm;
    /***** Bottom Right Panel *****/
    panel_br->style_btn = &style_btn_btm;
    panel_br->style_btn_pressed = &style_btn_pressed;
    panel_br->style_label = &style_label_btm;

    LV_UNUSED(style_btn_top);
    LV_UNUSED(style_btn_btm);
    LV_UNUSED(style_label_top);
    LV_UNUSED(style_label_btm);
}

void Layout::open_window()
{
    lv_screen_load(window);
}

void Layout::init_window()
{
    window = lv_obj_create(NULL);
    lv_obj_set_size(window, LCD_HORZ, LCD_VERT);
    lv_obj_center(window);
    lv_obj_add_style(window, &style_window, 0);
    /* Set grid layout type */
    lv_obj_set_grid_dsc_array(window, col_dsc, row_dsc);
    /* Remove padding from all objects to fit them closer together */
    lv_obj_set_style_pad_all(window, 0, 0);    // Remove padding around border of display
    lv_obj_set_style_pad_column(window, 0, 0); // Remove padding from columns between objects
    lv_obj_set_style_pad_row(window, 0, 0);    // Remove padding from rows between objects
    /* Turn off scrolling */
    lv_obj_set_scrollbar_mode(window, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(window, LV_OBJ_FLAG_SCROLLABLE);
}

void Layout::init_objects()
{
    /***** Top Left Panel *****/
    init_single_object(panel_tl);
    /***** Top Center Panel *****/
    init_single_object(panel_tc);
    /***** Top Right Panel *****/
    init_single_object(panel_tr);
    /***** Bottom Left Panel *****/
    init_single_object(panel_bl);
    /***** Bottom Right Panel *****/
    init_single_object(panel_br);
    /***** Set Locations *****/
    init_panel_locations();
}

void Layout::close_window()
{
    // Unsure if to do anything for this
}

void Layout::update_layout(Panel *new_panel, Panel_Location old_panel_location)
{
    /* Check what panel you want to switch out and replace it with the new one */
    switch (old_panel_location)
    {
    case Layout::TOP_LEFT:
        Layout::panel_tl = new_panel;
        break;

    case Layout::TOP_CENTER:
        Layout::panel_tc = new_panel;
        break;

    case Layout::TOP_RIGHT:
        Layout::panel_tr = new_panel;
        break;

    case Layout::BOTTOM_LEFT:
        Layout::panel_bl = new_panel;
        break;

    case Layout::BOTTOM_RIGHT:
        Layout::panel_br = new_panel;
        break;

    default:
        // Do nothing
        break;
    }

    update_info_list();
}

void Layout::set_led_on(Panel *panel, bool led_on)
{
    if (led_on)
    {
        lv_obj_set_size(panel->led, 20, 20);
        lv_obj_align(panel->led, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_led_on(panel->led);
    }
    else
    {
        lv_obj_set_size(panel->led, 10, 10);
        lv_obj_align(panel->led, LV_ALIGN_TOP_LEFT, 5, 5);
        lv_led_off(panel->led);
    }
}

void Layout::set_warning(Panel *panel, bool warning_on)
{
    if (warning_on)
    {
        lv_obj_set_style_text_color(panel->data, lv_palette_lighten(LV_PALETTE_RED, 1), 0);
        set_led_on(panel, true);
    }
    else
    {
        lv_obj_set_style_text_color(panel->data, lv_palette_lighten(LV_PALETTE_GREY, 5), 0);
        set_led_on(panel, false);
    }
}

/****************************
 * Private Helper Functions *
 ****************************/

void Layout::init_single_object(Panel *panel)
{
    bool led = 1;

    /* Button */
    panel->btn = lv_btn_create(window);                                       // Create the button in the Layout window
    lv_obj_add_style(panel->btn, panel->style_btn, 0);                        // Add the style for the button
    lv_obj_add_style(panel->btn, panel->style_btn_pressed, LV_STATE_PRESSED); // Add the style for the button when pressed
    /* Data in Button */
    panel->data = lv_label_create(panel->btn);                                 // Create a spot for data to go into the button
    lv_label_set_text(panel->data, panel->panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
    lv_obj_align(panel->data, LV_ALIGN_CENTER, 0, 10);                          // Put the data into the center of the button
    /* Label */
    panel->label = lv_label_create(window);                                           // Create a label to show what data the button is holding
    lv_label_set_text(panel->label, panel->panel_info->get_name_and_label().c_str()); // Put the name of the Information object into the label
    lv_obj_add_style(panel->label, panel->style_label, 0);                            // Add the style for the label
    /* Warning LED */
    if (led) // Base this off of configuration settings(?)
    {
        panel->led = lv_led_create(panel->btn);
        lv_obj_align(panel->led, LV_ALIGN_TOP_LEFT, 5, 5);
        lv_led_set_brightness(panel->led, 255);
        lv_led_set_color(panel->led, lv_palette_main(LV_PALETTE_RED));
        lv_obj_set_size(panel->led, 10, 10);
        lv_led_off(panel->led);
    }
    else
    {
        panel->led = lv_led_create(NULL); // Don't create an LED, but still need to potentially access it
    }
}

void Layout::init_panel_locations()
{
    /***** Top Left Panel *****/
    /* Button */
    lv_obj_set_grid_cell(panel_tl->btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Move the Panel button to the top left of the window
    /* Label */
    lv_obj_set_grid_cell(panel_tl->label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Move the Panel label below the Panel button
    /***** Top Center Panel *****/
    /* Button */
    lv_obj_set_grid_cell(panel_tc->btn, LV_GRID_ALIGN_STRETCH, 2, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Move the Panel button to the top center of the window
    /* Label */
    lv_obj_set_grid_cell(panel_tc->label, LV_GRID_ALIGN_CENTER, 2, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Move the Panel label below the Panel button
    /***** Top Right Panel *****/
    /* Button */
    lv_obj_set_grid_cell(panel_tr->btn, LV_GRID_ALIGN_STRETCH, 4, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Move the Panel button to the top right of the window
    /* Label */
    lv_obj_set_grid_cell(panel_tr->label, LV_GRID_ALIGN_CENTER, 4, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Move the Panel label below the Panel button
    /***** Bottom Left Panel *****/
    /* Button */
    lv_obj_set_grid_cell(panel_bl->btn, LV_GRID_ALIGN_STRETCH, 0, 3, LV_GRID_ALIGN_STRETCH, 2, 1); // Move the Panel button to the bottom left of the window
    /* Label */
    lv_obj_set_grid_cell(panel_bl->label, LV_GRID_ALIGN_CENTER, 0, 3, LV_GRID_ALIGN_CENTER, 3, 1); // Move the Panel label below the Panel button
    /***** Bottom Right Panel *****/
    /* Button */
    lv_obj_set_grid_cell(panel_br->btn, LV_GRID_ALIGN_STRETCH, 3, 3, LV_GRID_ALIGN_STRETCH, 2, 1); // Move the Panel button to the bottom right of the window
    /* Label */
    lv_obj_set_grid_cell(panel_br->label, LV_GRID_ALIGN_CENTER, 3, 3, LV_GRID_ALIGN_CENTER, 3, 1); // Move the Panel label below the Panel button
}

void Layout::update_info_list()
{
}