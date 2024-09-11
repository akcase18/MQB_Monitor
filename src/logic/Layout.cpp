#include "Layout.hpp"

/****************************
 * Layout Object Definition *
 ****************************/

DMAMEM Layout layout = Layout(panel_tl, panel_tc, panel_tr, panel_bl, panel_br); // Main layout definition

/****************
 * Constructors *
 ****************/

Layout::Layout()
{
    /* EMPTY FOR A REASON. KINDA USELESS */
}

Layout::Layout(Panel top_left, Panel top_center, Panel top_right, Panel bottom_left, Panel bottom_right)
{
    panel_tl = top_left;
    panel_tc = top_center;
    panel_tr = top_right;
    panel_bl = bottom_left;
    panel_br = bottom_right;

    // main_style_init();
    // setup_window();
    // setup_objects();
}

void Layout::open_window()
{
    // lv_screen_load(window);
}

void Layout::close_window()
{
    // Not sure what needs to be done here
}

// void Layout::setup_window()
// {
//     window = lv_obj_create(NULL);                // Create the Layout (Main) window in the background
//     lv_obj_set_size(window, LCD_HORZ, LCD_VERT); // Set to the proper window size
//     lv_obj_center(window);                       // Puts window in the center of the display
//     lv_obj_add_style(window, style_window, 0);   // Add the style to the window
//     /* Set grid layout type */
//     lv_obj_set_grid_dsc_array(window, col_dsc, row_dsc); // Adds columns and rows to the window
//     /* Remove padding around all objects to fit them closer together */
//     lv_obj_set_style_pad_all(window, 0, 0);    // Remove padding around border of display
//     lv_obj_set_style_pad_column(window, 0, 0); // Remove padding from columns between objects
//     lv_obj_set_style_pad_row(window, 0, 0);    // Remove padding from rows between objects
//     /* Turn off scrolling */
//     lv_obj_set_scrollbar_mode(window, LV_SCROLLBAR_MODE_OFF);
//     lv_obj_clear_flag(window, LV_OBJ_FLAG_SCROLLABLE);
// }

// void Layout::setup_objects()
// {
//     /***** Top Left Panel *****/
//     /* Button */
//     panel_tl.btn = lv_btn_create(window);                                                         // Create the button in the Layout window
//     lv_obj_add_style(panel_tl.btn, panel_tl.style_btn, 0);                                        // Add the style for the button
//     lv_obj_add_style(panel_tl.btn, panel_tl.style_btn_pressed, LV_STATE_PRESSED);                 // Add the style for the button when pressed
//     lv_obj_set_grid_cell(panel_tl.btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Organize the Panel into the correct spot in the grid
//     /* Data in Button */
//     panel_tl.data_btn = lv_label_create(panel_tl.btn);                                 // Create a spot for data to go into the button
//     lv_label_set_text(panel_tl.data_btn, panel_tl.panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
//     lv_obj_align(panel_tl.data_btn, LV_ALIGN_CENTER, 0, 0);                            // Put the data into the center of the button
//     /* Label */
//     panel_tl.label = lv_label_create(window);                                                     // Create a label to show what data the button is holding
//     lv_label_set_text(panel_tl.label, panel_tl.panel_info->get_name().c_str());                   // Put the name of the Information object into the label
//     lv_obj_add_style(panel_tl.label, panel_tl.style_label, 0);                                    // Add the style for the label
//     lv_obj_set_grid_cell(panel_tl.label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Put the label right below the button
//     /***** Top Center Panel *****/
//     /* Button */
//     panel_tc.btn = lv_btn_create(window);                                                         // Create the button in the Layout window
//     lv_obj_add_style(panel_tc.btn, panel_tc.style_btn, 0);                                        // Add the style for the button
//     lv_obj_add_style(panel_tc.btn, panel_tc.style_btn_pressed, LV_STATE_PRESSED);                 // Add the style for the button when pressed
//     lv_obj_set_grid_cell(panel_tc.btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Organize the Panel into the correct spot in the grid
//     /* Data in Button */
//     panel_tc.data_btn = lv_label_create(panel_tc.btn);                                 // Create a spot for data to go into the button
//     lv_label_set_text(panel_tc.data_btn, panel_tc.panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
//     lv_obj_align(panel_tc.data_btn, LV_ALIGN_CENTER, 0, 0);                            // Put the data into the center of the button
//     /* Label */
//     panel_tc.label = lv_label_create(window);                                                     // Create a label to show what data the button is holding
//     lv_label_set_text(panel_tc.label, panel_tc.panel_info->get_name().c_str());                   // Put the name of the Information object into the label
//     lv_obj_add_style(panel_tc.label, panel_tc.style_label, 0);                                    // Add the style for the label
//     lv_obj_set_grid_cell(panel_tc.label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Put the label right below the button
//     /***** Top Right Panel *****/
//     /* Button */
//     panel_tr.btn = lv_btn_create(window);                                                         // Create the button in the Layout window
//     lv_obj_add_style(panel_tr.btn, panel_tr.style_btn, 0);                                        // Add the style for the button
//     lv_obj_add_style(panel_tr.btn, panel_tr.style_btn_pressed, LV_STATE_PRESSED);                 // Add the style for the button when pressed
//     lv_obj_set_grid_cell(panel_tr.btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Organize the Panel into the correct spot in the grid
//     /* Data in Button */
//     panel_tr.data_btn = lv_label_create(panel_tr.btn);                                 // Create a spot for data to go into the button
//     lv_label_set_text(panel_tr.data_btn, panel_tr.panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
//     lv_obj_align(panel_tr.data_btn, LV_ALIGN_CENTER, 0, 0);                            // Put the data into the center of the button
//     /* Label */
//     panel_tr.label = lv_label_create(window);                                                     // Create a label to show what data the button is holding
//     lv_label_set_text(panel_tr.label, panel_tr.panel_info->get_name().c_str());                   // Put the name of the Information object into the label
//     lv_obj_add_style(panel_tr.label, panel_tr.style_label, 0);                                    // Add the style for the label
//     lv_obj_set_grid_cell(panel_tr.label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Put the label right below the button
//     /***** Bottom Left Panel *****/
//     /* Button */
//     panel_bl.btn = lv_btn_create(window);                                                         // Create the button in the Layout window
//     lv_obj_add_style(panel_bl.btn, panel_bl.style_btn, 0);                                        // Add the style for the button
//     lv_obj_add_style(panel_bl.btn, panel_bl.style_btn_pressed, LV_STATE_PRESSED);                 // Add the style for the button when pressed
//     lv_obj_set_grid_cell(panel_bl.btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Organize the Panel into the correct spot in the grid
//     /* Data in Button */
//     panel_bl.data_btn = lv_label_create(panel_bl.btn);                                 // Create a spot for data to go into the button
//     lv_label_set_text(panel_bl.data_btn, panel_bl.panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
//     lv_obj_align(panel_bl.data_btn, LV_ALIGN_CENTER, 0, 0);                            // Put the data into the center of the button
//     /* Label */
//     panel_bl.label = lv_label_create(window);                                                     // Create a label to show what data the button is holding
//     lv_label_set_text(panel_bl.label, panel_bl.panel_info->get_name().c_str());                   // Put the name of the Information object into the label
//     lv_obj_add_style(panel_bl.label, panel_bl.style_label, 0);                                    // Add the style for the label
//     lv_obj_set_grid_cell(panel_bl.label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Put the label right below the button
//     /***** Bottom Right Panel *****/
//     /* Button */
//     panel_br.btn = lv_btn_create(window);                                                         // Create the button in the Layout window
//     lv_obj_add_style(panel_br.btn, panel_br.style_btn, 0);                                        // Add the style for the button
//     lv_obj_add_style(panel_br.btn, panel_br.style_btn_pressed, LV_STATE_PRESSED);                 // Add the style for the button when pressed
//     lv_obj_set_grid_cell(panel_br.btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Organize the Panel into the correct spot in the grid
//     /* Data in Button */
//     panel_br.data_btn = lv_label_create(panel_br.btn);                                 // Create a spot for data to go into the button
//     lv_label_set_text(panel_br.data_btn, panel_br.panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
//     lv_obj_align(panel_br.data_btn, LV_ALIGN_CENTER, 0, 0);                            // Put the data into the center of the button
//     /* Label */
//     panel_br.label = lv_label_create(window);                                                     // Create a label to show what data the button is holding
//     lv_label_set_text(panel_br.label, panel_br.panel_info->get_name().c_str());                   // Put the name of the Information object into the label
//     lv_obj_add_style(panel_br.label, panel_br.style_label, 0);                                    // Add the style for the label
//     lv_obj_set_grid_cell(panel_br.label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Put the label right below the button
// }

// void Layout::main_style_init()
// {
//     lv_style_init(style_window);
//     lv_style_set_bg_color(style_window, lv_palette_darken(LV_PALETTE_GREY, 4));
//     lv_style_set_border_width(style_window, 0);
//     lv_style_set_radius(style_window, 0);
// }

void Layout::update_layout(Panel new_panel, Panel_Location old_panel)
{
    /* Check what panel you want to switch out and replace it with the new one */
    switch (old_panel)
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

void Layout::update_info_list()
{
    vector<Information> new_info_list; // Create new vector of Information objects
    /* Add Information objects from Panels into new vector */
    new_info_list.push_back(panel_tl.panel_info);
    new_info_list.push_back(panel_tc.panel_info);
    new_info_list.push_back(panel_tr.panel_info);
    new_info_list.push_back(panel_bl.panel_info);
    new_info_list.push_back(panel_br.panel_info);

    Layout::info_list.swap(new_info_list); // Swap contents of new list into old list
}