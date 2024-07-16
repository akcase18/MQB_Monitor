#include "Layout.hpp"

/****************
 * Constructors *
 ****************/

Layout::Layout()
{
}

Layout::Layout(Panel top_left, Panel top_center, Panel top_right, Panel bottom_left, Panel bottom_right)
{
    
}

void Layout::update_layout(Panel new_panel, Panel_Number old_panel)
{
    switch (old_panel)
    {
    case Layout::TOP_LEFT:
        Layout::top_left_panel = new_panel;
        break;

    case Layout::TOP_CENTER:
        Layout::top_center_panel = new_panel;
        break;

    case Layout::TOP_RIGHT:
        Layout::top_right_panel = new_panel;
        break;

    case Layout::BOTTOM_LEFT:
        Layout::bottom_left_panel = new_panel;
        break;

    case Layout::BOTTOM_RIGHT:
        Layout::bottom_right_panel = new_panel;
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
    new_info_list.push_back(Layout::top_left_panel.panel_info);
    new_info_list.push_back(Layout::top_center_panel.panel_info);
    new_info_list.push_back(Layout::top_right_panel.panel_info);
    new_info_list.push_back(Layout::bottom_left_panel.panel_info);
    new_info_list.push_back(Layout::bottom_right_panel.panel_info);

    Layout::info_list.swap(new_info_list); // Swap contents of new list into old list
}