#ifndef LAYOUT_H
#define LAYOUT_H

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

    static Panel top_left_panel;          // Panel in the top left position
    static Panel top_center_panel;        // Panel in the top center position
    static Panel top_right_panel;         // Panel in the top right position
    static Panel bottom_left_panel;       // Panel in the bottom left position
    static Panel bottom_right_panel;      // Panel in the bottom right position
    static vector<Information> info_list; // Stores the Information objects that are on screen

    /********************
     * Public Functions *
     ********************/

    /**
     * @brief Default constructor for Layout.
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

#endif