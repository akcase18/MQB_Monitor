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

    static Information *panel_info;   // The Information object for this Panel
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
     * 
     * Please don't use. Kinda useless cuz you kinda need information for this
     * shit
     */
    Panel();

    /**
     * @brief Main constructor for Panel objects. Creates an Information object
     * using the passed-in pointer, and sets the Panel's panel_info variable to
     * that Information object. Doesn't initialize any button or label styles.
     *
     * @param info Pointer to an existing Information object
     * @param panel_button Pointer to lv_obj_t for a button
     * @param panel_button_data Pointer to lv_obj_t for button data
     * @param panel_label Pointer to lv_obj_t for a label
     */
    Panel(Information *info, lv_obj_t *panel_button, lv_obj_t *panel_button_data, lv_obj_t *panel_label);

    /**
     * @brief Main constructor for Panel objects. Creates an Information object
     * using the passed-in pointer, and sets the Panel's panel_info variable to
     * that Information object. Initializes button and label styles.
     *
     * @param info Pointer to an existing Information object
     * @param panel_button Pointer to lv_obj_t for a button
     * @param panel_button_data Pointer to lv_obj_t for button data
     * @param panel_label Pointer to lv_obj_t for a label
     * @param panel_button_style Pointer to lv_style_t for button style
     * @param panel_label_style Pointer to lv_style_t for label style
     */
    Panel(Information *info, lv_obj_t *panel_button, lv_obj_t *panel_button_data, lv_obj_t *panel_label, lv_style_t *panel_button_style, lv_style_t *panel_label_style);

    /* Might not use these */

    void set_button_style(lv_style_t *style);
    lv_style_t *get_button_style();

    void set_label_style(lv_style_t *style);
    lv_style_t *get_label_style();
};

#endif