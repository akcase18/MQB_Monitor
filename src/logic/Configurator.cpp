#include "Configurator.hpp"

/****************
 * Constructors *
 ****************/

Configurator::Configurator()
{
    config_style_init();
    setup_config_window();
}

/***************************
 * Public Helper Functions *
 ***************************/

void Configurator::show_config_window()
{
    window = lv_obj_create(lv_scr_act()); // Sets the window to be located on the active screen
    lv_scr_load(window);                  // Sets the window to be located on the active screen
}

void Configurator::close_config_window()
{
    get_info_from_drop_down();
}

void Configurator::create_done_btn()
{
    btn_done = lv_obj_create(window);
    lv_obj_set_size(btn_done, 90, 50);
    lv_obj_align(btn_done, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_style(btn_done, style_btn_done, 0);
    lv_obj_add_style(btn_done, style_btn_done, LV_STATE_PRESSED);
    // lv_obj_add_event_cb(btn_done, close_window_cb, LV_EVENT_ALL, window);
    /* Turn off scrolling */
    lv_obj_set_scrollbar_mode(btn_done, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(btn_done, LV_OBJ_FLAG_SCROLLABLE);
}

void Configurator::create_radio_btns()
{
    radio_imperial = lv_checkbox_create(flex_col_2); // Add radio button to Column 2 of flex layout
    lv_obj_add_flag(radio_imperial, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_style(radio_imperial, style_radio_unchecked, LV_PART_INDICATOR);                  // Add style for when not selected
    lv_obj_add_style(radio_imperial, style_radio_checked, LV_PART_INDICATOR | LV_STATE_CHECKED); // Add style for when selected
    lv_checkbox_set_text(radio_imperial, "Imperial");                                            // Set text for radio button
    lv_obj_align(radio_imperial, LV_ALIGN_TOP_MID, lv_pct(0), lv_pct(0));                        // Align radio button on top

    radio_metric = lv_checkbox_create(flex_col_2); // Add radio button to Column 2 of flex layout
    lv_obj_add_flag(radio_metric, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_style(radio_metric, style_radio_unchecked, LV_PART_INDICATOR);                  // Add style for when not selected
    lv_obj_add_style(radio_metric, style_radio_checked, LV_PART_INDICATOR | LV_STATE_CHECKED); // Add style for when selected
    lv_checkbox_set_text(radio_metric, "Metric");                                              // Set text for radio button
    lv_obj_align(radio_metric, LV_ALIGN_TOP_MID, lv_pct(0), lv_pct(25));                       // Align radio button on bottom
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
    window = lv_obj_create(NULL);              // Create the Configurator window in the background
    lv_obj_set_size(window, 480, 320);         // Set to the proper window size
    lv_obj_center(window);                     // Puts window in the center of the display
    lv_obj_add_style(window, style_window, 0); // Add the style to the window
    /* Turn off scrolling */
    lv_obj_set_scrollbar_mode(window, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(window, LV_OBJ_FLAG_SCROLLABLE);
    /* Add formatting to window */
    flex_col_1 = lv_obj_create(window);                    // Adds flex column to the Configurator window
    lv_obj_set_flex_flow(flex_col_1, LV_FLEX_FLOW_COLUMN); // Sets column to use flex flow layout
    lv_obj_set_size(flex_col_1, lv_pct(60), lv_pct(100));  // Set column to take up 60% of window width
    lv_obj_add_style(flex_col_1, style_layout, 0);         // Add flex layout style to column
    flex_col_2 = lv_obj_create(window);                    // Adds flex clumn to the Configurator window
    lv_obj_set_flex_flow(flex_col_2, LV_FLEX_FLOW_COLUMN); // Sets column to use flex flow layout
    lv_obj_set_size(flex_col_2, lv_pct(40), lv_pct(100));  // Set column to take up 40% of window width
    // lv_obj_add_event_cb(flex_col_2, radio_cb, LV_EVENT_CLICKED, &active_index);
    lv_obj_set_x(flex_col_2, lv_pct(60));          // Set column to take up 60% of window height
    lv_obj_add_style(flex_col_2, style_layout, 0); // Add flex layout style to column
    /***** Add elements to window *****/
    /* Drop Down */
    String drop_down_string = ""; // String to hold all values for the drop-down menu
    create_drop_down_list();
    for (uint i = 0; i < drop_down_list.size(); i++)
    {
        drop_down_string += drop_down_list[i] + "\n";
    }
    drop_down = lv_dropdown_create(flex_col_1);                          // Add drop-down menu to first column
    lv_dropdown_set_options_static(drop_down, drop_down_string.c_str()); // Add list of options to drop-down menu
    lv_obj_align(drop_down, LV_ALIGN_TOP_MID, 0, 0);                     // Put drop-dwon in the top-center of the first column
    lv_obj_set_size(drop_down, lv_pct(100), 55);                         // Let the drop-down take up the entire width of the first column, and 55px height
    /* Unit Radio Button */
    create_radio_btns();
    // Start with both unchecked, and select the correct one when the window opens
    /* Done Button */
    create_done_btn();
}

void Configurator::create_drop_down_list()
{
    for (uint i = 0; i < info_list.size(); i++)
    {
        drop_down_list.push_back(info_list[i].get_name());
    }
}

Information *Configurator::get_info_from_drop_down()
{
    uint8_t index = lv_dropdown_get_selected(drop_down);
    String info_name = drop_down_list[index];
}
