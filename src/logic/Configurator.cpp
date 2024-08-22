#include "Configurator.hpp"

/****************
 * Constructors *
 ****************/

Configurator::Configurator()
{
    config_style_init();
}

/***************************
 * Public Helper Functions *
 ***************************/

void Configurator::show_config_window()
{
    window = lv_obj_create(lv_scr_act()); // Sets the window to be located on the active screen
}

void Configurator::close_config_window()
{
    get_info_from_drop_down(drop_down);
}

void Configurator::create_done_btn()
{
    btn_done = lv_obj_create(window);
    lv_obj_set_size(btn_done, 90, 50);
    lv_obj_align(btn_done, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_style(btn_done, style_btn_done, 0);
    lv_obj_add_style(btn_done, style_btn_done, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_done, close_window_cb, LV_EVENT_ALL, window);
    /* Turn off scrolling */
    lv_obj_set_scrollbar_mode(btn_done, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(btn_done, LV_OBJ_FLAG_SCROLLABLE);
}

/****************************
 * Private Helper Functions *
 ****************************/

void Configurator::config_style_init()
{
    /* Set the style of the configuration window */
    lv_style_init(style_window);                                                // Initialize the style for the window
    lv_style_set_bg_color(style_window, lv_palette_darken(LV_PALETTE_GREY, 4)); // Set the background color to 4 steps darker than base grey

    /* Set the style of the "done" button */
    lv_style_init(style_btn_done);                                                   // Initialize the style for the done button
    lv_style_set_radius(style_btn_done, 5);                                          // Set the radius of the border to 5 pixels
    lv_style_set_opa(style_btn_done, LV_OPA_100);                                    // Set to 100% opacity
    lv_style_set_bg_color(style_btn_done, lv_palette_darken(LV_PALETTE_GREY, 4));    // Set the background color to 4 steps darker than base grey
    lv_style_set_text_color(style_btn_done, lv_palette_lighten(LV_PALETTE_GREY, 4)); // Set the text color to 4 steps lighter than base grey
    lv_style_set_text_font(style_btn_done, &vw_24);                                  // Set the font
    lv_style_set_text_letter_space(style_btn_done, 0);                               // Remove as much space as possible from between the letters
    lv_style_set_shadow_width(style_btn_done, 0);                                    // Remove the drop shadow

    /* Set the style of the radio button for selecting unit region */
    lv_style_init(style_radio_unchecked);                                                   // Initialize the style for the radio button
    lv_style_set_radius(style_radio_unchecked, LV_RADIUS_CIRCLE);                           // Make the buttons a circle
    lv_style_set_text_color(style_radio_unchecked, lv_palette_lighten(LV_PALETTE_GREY, 5)); // Set the text color to 5 steps lighter than base grey
    /* Set the style of the radio button when checked */
    lv_style_init(style_radio_checked);                 // Initialize the style for when a radio button is selected
    lv_style_set_bg_img_src(style_radio_checked, NULL); // Remove any special background image for when a radio button is selected

    /* Set the style for the flex layout objects */
    lv_style_init(style_layout);                                                   // Initialize the style for the objects in the layout
    lv_style_set_bg_color(style_layout, lv_palette_darken(LV_PALETTE_GREY, 4));    // Set the background color to 4 steps darker than base grey
    lv_style_set_border_width(style_layout, 0);                                    // Remove border
    lv_style_set_text_color(style_layout, lv_palette_lighten(LV_PALETTE_GREY, 5)); // Set the text color to 5 steps lighter than base grey
    lv_style_set_text_font(style_layout, &vw_24);                                  // Set the font to the same font as the drop-down menu
}

void Configurator::setup_config_window()
{
    lv_obj_set_size(window, 480, 320);         // Set to the proper window size
    lv_obj_center(window);                     // Puts window in the center of the display
    lv_obj_add_style(window, style_window, 0); // Add the style to the window
    /* Turn off scrolling */
    lv_obj_set_scrollbar_mode(window, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(window, LV_OBJ_FLAG_SCROLLABLE);
    /* Add elements to window */
    String drop_down_string = ""; // String to hold all values for the drop-down menu
    for (uint i = 0; i < drop_down_list.size(); i++)
    {
        drop_down_string += drop_down_list[i] + "\n";
    }
}

void Configurator::create_drop_down_list()
{
    for (uint i = 0; i < Information::information_list.size(); i++)
    {
        drop_down_list.push_back(Information::information_list[i]->get_name());
    }
}

Information *Configurator::get_info_from_drop_down(lv_obj_t *drop_down)
{
    uint8_t index = lv_dropdown_get_selected(drop_down);
    String info_name = drop_down_list[index];
}

/*************
 * Callbacks *
 *************/

void Configurator::close_window_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        close_config_window();
    }
}