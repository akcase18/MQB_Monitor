#ifndef LAYOUT_H
#define LAYOUT_H

/**
 * This is the Layout class.
 *
 * The Layout class is an organization of Panel objects. Currently,
 * it only supports a 5-Panel configuration, with 3 Panels on the top
 * row, 2 Panels on the bottom row, and the display in a landscape
 * orientation. Eventually, this will support multiple different
 * display configurations, through this may be done with different
 * classes rather than expanding functionality of this class. Each
 * Layout class holds:
 * -> A window object to hold all elements
 * -> A number (5) of Panel objects
 * -> A list of all Information objects in the Panels
 *
 * Used in:
 * -> Configurator (But only kinda)
 */

#include <vector>
#include "Panel.hpp"

using namespace std;

/********************
 * Global Constants *
 ********************/

class Layout
{
private:
    /*********************
     * Private Constants *
     *********************/

public:
    /********************
     * Public Constants *
     ********************/

    enum Panel_Location
    {
        TOP_LEFT = 0,
        TOP_CENTER,
        TOP_RIGHT,
        BOTTOM_LEFT,
        BOTTOM_RIGHT
    };

private:
    /*********************
     * Private Variables *
     *********************/

    /*********************
     * Private Functions *
     *********************/

public:
    /********************
     * Public Variables *
     ********************/

    lv_obj_t *window;              // Window object that holds all other elements
    Panel *panel_tl;               // Panel in the top left position
    Panel *panel_tc;               // Panel in the top center position
    Panel *panel_tr;               // Panel in the top right position
    Panel *panel_bl;               // Panel in the bottom left position
    Panel *panel_br;               // Panel in the bottom right position
    lv_style_t *style_window;      // The style of the window
    static vector<Information> info_list; // Stores the Information objects that are on screen

    /********************
     * Public Functions *
     ********************/

    /**
     * @brief Default constructor for Layout. Don't really use this. Ever.
     */
    Layout();

    /**
     * @brief Main constructor for Layout. Sets up and keeps track of the Panel objects
     * that are being displayed on the screen, and will eventually keep track of button
     * presses, screen states, and different screen layouts.
     *
     * @param top_left The Panel object that will be in the top left position
     * @param top_center The Panel object that will be in the top center position
     * @param top_right The Panel object that will be in the top right position
     * @param bottom_left The Panel object that will be in the bottom left position
     * @param bottom_right The Panel object that will be in the bottom right position
     */
    Layout(Panel *top_left, Panel *top_center, Panel *top_right, Panel *bottom_left, Panel *bottom_right);

    /**
     * @brief Updates the layout by replacing an old Panel object with a new Panel
     * object.
     *
     * @param new_panel Panel object that will be added
     * @param old_panel Panel object to replace
     */
    void update_layout(Panel *new_panel, Panel_Location old_panel);

    /**
     * @brief Ensures that the info_list is up to date with the most recent Panel
     * objects that are in the Layout.
     */
    void update_info_list();
};

#endif