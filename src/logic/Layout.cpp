#include "Layout.hpp"

/****************
 * Constructors *
 ****************/

Layout::Layout()
{
    /* EMPTY FOR A REASON. KINDA USELESS */
}

Layout::Layout(Panel *top_left, Panel *top_center, Panel *top_right, Panel *bottom_left, Panel *bottom_right)
{
    panel_tl = top_left;
    panel_tc = top_center;
    panel_tr = top_right;
    panel_bl = bottom_left;
    panel_br = bottom_right;
}

void Layout::setup_objects()
{
    /***** Top Left Panel *****/
    /* Button */
    panel_tl->btn = lv_obj_create(window);                                                         // Create the button in the Layout window
    lv_obj_add_style(panel_tl->btn, panel_tl->style_btn, 0);                                       // Add the style for the button
    lv_obj_add_style(panel_tl->btn, panel_tl->style_btn_pressed, LV_STATE_PRESSED);                // Add the style for the button when pressed
    lv_obj_set_grid_cell(panel_tl->btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Organize the Panel into the correct spot in the grid
    /* Data in Button */
    panel_tl->data_btn = lv_label_create(panel_tl->btn);                                 // Create a spot for data to go into the button
    lv_label_set_text(panel_tl->data_btn, panel_tl->panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
    lv_obj_align(panel_tl->data_btn, LV_ALIGN_CENTER, 0, 0);                             // Put the data into the center of the button
    /* Label */
    panel_tl->label = lv_label_create(window);                                                     // Create a label to show what data the button is holding
    lv_label_set_text(panel_tl->label, panel_tl->panel_info->get_name().c_str());                  // Put the name of the Information object into the label
    lv_obj_add_style(panel_tl->label, panel_tl->style_label, 0);                                   // Add the style for the label
    lv_obj_set_grid_cell(panel_tl->label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Put the label right below the button
    /***** Top Center Panel *****/
    /* Button */
    panel_tc->btn = lv_obj_create(window);                                                         // Create the button in the Layout window
    lv_obj_add_style(panel_tc->btn, panel_tc->style_btn, 0);                                       // Add the style for the button
    lv_obj_add_style(panel_tc->btn, panel_tc->style_btn_pressed, LV_STATE_PRESSED);                // Add the style for the button when pressed
    lv_obj_set_grid_cell(panel_tc->btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Organize the Panel into the correct spot in the grid
    /* Data in Button */
    panel_tc->data_btn = lv_label_create(panel_tc->btn);                                 // Create a spot for data to go into the button
    lv_label_set_text(panel_tc->data_btn, panel_tc->panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
    lv_obj_align(panel_tc->data_btn, LV_ALIGN_CENTER, 0, 0);                             // Put the data into the center of the button
    /* Label */
    panel_tc->label = lv_label_create(window);                                                     // Create a label to show what data the button is holding
    lv_label_set_text(panel_tc->label, panel_tc->panel_info->get_name().c_str());                  // Put the name of the Information object into the label
    lv_obj_add_style(panel_tc->label, panel_tc->style_label, 0);                                   // Add the style for the label
    lv_obj_set_grid_cell(panel_tc->label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Put the label right below the button
    /***** Top Right Panel *****/
    /* Button */
    panel_tr->btn = lv_obj_create(window);                                                         // Create the button in the Layout window
    lv_obj_add_style(panel_tr->btn, panel_tr->style_btn, 0);                                       // Add the style for the button
    lv_obj_add_style(panel_tr->btn, panel_tr->style_btn_pressed, LV_STATE_PRESSED);                // Add the style for the button when pressed
    lv_obj_set_grid_cell(panel_tr->btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Organize the Panel into the correct spot in the grid
    /* Data in Button */
    panel_tr->data_btn = lv_label_create(panel_tr->btn);                                 // Create a spot for data to go into the button
    lv_label_set_text(panel_tr->data_btn, panel_tr->panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
    lv_obj_align(panel_tr->data_btn, LV_ALIGN_CENTER, 0, 0);                             // Put the data into the center of the button
    /* Label */
    panel_tr->label = lv_label_create(window);                                                     // Create a label to show what data the button is holding
    lv_label_set_text(panel_tr->label, panel_tr->panel_info->get_name().c_str());                  // Put the name of the Information object into the label
    lv_obj_add_style(panel_tr->label, panel_tr->style_label, 0);                                   // Add the style for the label
    lv_obj_set_grid_cell(panel_tr->label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Put the label right below the button
    /***** Bottom Left Panel *****/
    /* Button */
    panel_bl->btn = lv_obj_create(window);                                                         // Create the button in the Layout window
    lv_obj_add_style(panel_bl->btn, panel_bl->style_btn, 0);                                       // Add the style for the button
    lv_obj_add_style(panel_bl->btn, panel_bl->style_btn_pressed, LV_STATE_PRESSED);                // Add the style for the button when pressed
    lv_obj_set_grid_cell(panel_bl->btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Organize the Panel into the correct spot in the grid
    /* Data in Button */
    panel_bl->data_btn = lv_label_create(panel_bl->btn);                                 // Create a spot for data to go into the button
    lv_label_set_text(panel_bl->data_btn, panel_bl->panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
    lv_obj_align(panel_bl->data_btn, LV_ALIGN_CENTER, 0, 0);                             // Put the data into the center of the button
    /* Label */
    panel_bl->label = lv_label_create(window);                                                     // Create a label to show what data the button is holding
    lv_label_set_text(panel_bl->label, panel_bl->panel_info->get_name().c_str());                  // Put the name of the Information object into the label
    lv_obj_add_style(panel_bl->label, panel_bl->style_label, 0);                                   // Add the style for the label
    lv_obj_set_grid_cell(panel_bl->label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Put the label right below the button
    /***** Bottom Right Panel *****/
    /* Button */
    panel_br->btn = lv_obj_create(window);                                                         // Create the button in the Layout window
    lv_obj_add_style(panel_br->btn, panel_br->style_btn, 0);                                       // Add the style for the button
    lv_obj_add_style(panel_br->btn, panel_br->style_btn_pressed, LV_STATE_PRESSED);                // Add the style for the button when pressed
    lv_obj_set_grid_cell(panel_br->btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Organize the Panel into the correct spot in the grid
    /* Data in Button */
    panel_br->data_btn = lv_label_create(panel_br->btn);                                 // Create a spot for data to go into the button
    lv_label_set_text(panel_br->data_btn, panel_br->panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
    lv_obj_align(panel_br->data_btn, LV_ALIGN_CENTER, 0, 0);                             // Put the data into the center of the button
    /* Label */
    panel_br->label = lv_label_create(window);                                                     // Create a label to show what data the button is holding
    lv_label_set_text(panel_br->label, panel_br->panel_info->get_name().c_str());                  // Put the name of the Information object into the label
    lv_obj_add_style(panel_br->label, panel_br->style_label, 0);                                   // Add the style for the label
    lv_obj_set_grid_cell(panel_br->label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Put the label right below the button
}

void Layout::update_layout(Panel *new_panel, Panel_Location old_panel)
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
    new_info_list.push_back(panel_tl->panel_info);
    new_info_list.push_back(panel_tc->panel_info);
    new_info_list.push_back(panel_tr->panel_info);
    new_info_list.push_back(panel_bl->panel_info);
    new_info_list.push_back(panel_br->panel_info);

    Layout::info_list.swap(new_info_list); // Swap contents of new list into old list
}