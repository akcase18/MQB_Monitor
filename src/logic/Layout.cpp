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