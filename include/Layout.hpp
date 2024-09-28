#ifndef LAYOUT_H
#define LAYOUT_H

#include <vector>
#include <lvgl.h>
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

    /**
     * Sets up the style of the window
     */
    void style_init();

    /**
     * Sets up the style of the Panels and the objects held in each
     * Panel
     */
    void panel_style_init();

    /**
     * Initialize the main window
     */
    void init_window();

    /**
     * Initialize the objects in the main window
     */
    void init_objects();

    /**
     * Initialize/setup single Panel object in the Layout window
     *
     * @param panel the Panel to set up
     */
    void init_single_object(Panel *panel);

    /**
     * Move the Panel object into the correct locations
     */
    void init_panel_locations();

    /**
     * Updates info_list to the most recent Information objects that are
     * in the Panel objects.
     */
    void update_info_list();

public:
    /********************
     * Public Variables *
     ********************/
    lv_obj_t *window;        // Window object to hold all other objects
    lv_style_t style_window; // Style for the window object
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
    lv_coord_t col_dsc[7] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
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
    lv_coord_t row_dsc[5] = {LV_GRID_FR(6), LV_GRID_FR(1), LV_GRID_FR(8), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    Panel *panel_tl;                   // Panel in the top left position
    Panel *panel_tc;                   // Panel in the top center position
    Panel *panel_tr;                   // Panel in the top right position
    Panel *panel_bl;                   // Panel in the bottom left position
    Panel *panel_br;                   // Panel in the bottom right position
    vector<Information *> active_info; // Stores on-screen Information objects

    /********************
     * Public Functions *
     ********************/

    /**
     * Default constructor for Layout. Don't use this, ever
     */
    Layout();

    /**
     * Main constructor for Layout object. Sets up and keeps track of the Panel objects
     * that are being displayed on screen, and will eventually keep track of button
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
     * Initializes the Layout object (outside of the constructor). This
     * is necessary because calling functions like style_init(), init_window(),
     * etc. rely on lv_init() to be called first, which isn't done until the
     * setup() in main.cpp.
     */
    void init_layout();

    /**
     * Opens the main window
     */
    void open_window();

    /**
     * Closes the main window
     */
    void close_window();

    /**
     * Updates the Layout with a new Panel object by replacing an old Panel object
     * with the new Panel object.
     *
     * @param new_panel the new Panel to add to the Layout
     * @param old_panel_location the location of the Panel to be replaced
     */
    void update_layout(Panel *new_panel, Panel_Location old_panel_location);
};

/*****************************
 * Layout Object Declaration *
 *****************************/

extern Layout layout;

#endif