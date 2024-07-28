#ifndef PANEL_H
#define PANEL_H

#include <lvgl.h>
#include "Information.hpp"

using namespace std;

/********************
 * Global Constants *
 ********************/

class Panel
{
private:
    /*********************
     * Private Constants *
     *********************/

public:
    /********************
     * Public Constants *
     ********************/

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

    static Information panel_info;   // The Information object for this Panel
    static lv_obj_t *button;         // The button this Panel handles
    static lv_obj_t *button_data;    // The data that goes inside of the button
    static lv_obj_t *label;          // The label associated with the button
    static lv_style_t *button_style; // The style of the button
    static lv_style_t *label_style;  // The style of the panel

    /********************
     * Public Functions *
     ********************/

    /**
     * @brief Default constructor for Panel objects. Creates a new Information
     * object using the default Information object constructor, and sets the Panel's
     * panel_info variable to that Information object.
     */
    Panel();

    /**
     * @brief Main constrcutor for Panel objects. Creates an Information object
     * using the passed-in pointer, and sets the Panel's panel_info variable to
     * that Information object.
     *
     * @param info Pointer to an existing Information object
     */
    Panel(Information *info);

    void set_button_style(lv_style_t *style);
    lv_style_t *get_button_style();

    void set_label_style(lv_style_t *style);
    lv_style_t *get_label_style();
};

#endif