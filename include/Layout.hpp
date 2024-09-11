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

#define LCD_HORZ 480 // Horizontal resolution of the display in pixels
#define LCD_VERT 320 // Vertical resolution of the display in pixels

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

    // lv_obj_t *window; // Window object that holds all other elements
    /**
     * Columns for the window layout
     * Column 1: Start of top left, bottom left panel
     * Column 2: End of top left panel
     * Column 3: Start of top center panel. End of bottom left panel
     * Column 4: Start of bottom right panel. End of top center panel
     * Column 5: Start of top right panel
     * Column 6: End of top right, bottom right panel
     *
     * Top panels take up 2 columns each
     * Bottom panels take up 3 columns each
     *
     * All columns are 80 px wide
     */
    // lv_coord_t col_dsc[7] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    /**
     * Rows for the window layout
     * Row 1: Top row of buttons
     * -> 6 units tall (120 px)
     * Row 2: Labels for top buttons
     * -> 1 unit tall (20 px)
     * Row 3: Bottom row of buttons
     * -> 8 units tall (160 px)
     * Row 4: Labels for bottom buttons
     * -> 1 unit tall (20 px)
     */
    // lv_coord_t row_dsc[5] = {LV_GRID_FR(6), LV_GRID_FR(1), LV_GRID_FR(8), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    Panel panel_tl;               // Panel in the top left position
    Panel panel_tc;               // Panel in the top center position
    Panel panel_tr;               // Panel in the top right position
    Panel panel_bl;               // Panel in the bottom left position
    Panel panel_br;               // Panel in the bottom right position
    // lv_style_t *style_window;      // The style of the window
    vector<Information> info_list; // Stores the Information objects that are on screen

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
    Layout(Panel top_left, Panel top_center, Panel top_right, Panel bottom_left, Panel bottom_right);

    /**
     * Displays the main Layout window.
     */
    void open_window();

    /**
     * Closes the main Layout window.
     */
    void close_window();

    /**
     * Setup the window with the correct parameters and layout.
     */
    // void setup_window();

    /**
     * Setup the layout of the Panels in the Layout object, put everything into
     * the correct places, and add all styles.
     */
    // void setup_objects();

    /**
     * Creates the style for the window itself
     */
    // void main_style_init();

    /**
     * @brief Updates the layout by replacing an old Panel object with a new Panel
     * object.
     *
     * @param new_panel Panel object that will be added
     * @param old_panel Panel object to replace
     */
    void update_layout(Panel new_panel, Panel_Location old_panel);

    /**
     * @brief Ensures that the info_list is up to date with the most recent Panel
     * objects that are in the Layout.
     */
    void update_info_list();
};

/*****************************
 * Layout Object Declaration *
 *****************************/

extern Layout layout;

#endif