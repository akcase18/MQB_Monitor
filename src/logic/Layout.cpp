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
    /* Data in Button */
    /* Label */

    /***** Top Right Panel *****/
    /***** Bottom Left Panel *****/
    /***** Bottom Right Panel *****/
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