#ifndef PANEL_H
#define PANEL_H

#include <lvgl.h>
#include "Information.hpp"

using namespace std;

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

    /**
     * Sets the Information object referenced by the Panel.
     *
     * @param info Information object for the panel
     */
    void set_info(Information *info);

    /**
     * Sets the button for the Panel.
     *
     * @param btn Button for the Panel
     */
    void set_btn(lv_obj_t *btn);

    /**
     * Sets the data that goes inside the button in the Panel.
     *
     * @param btn Data for the Panel
     */
    void set_data(lv_obj_t *data);

    /**
     * Sets the label for the Panel.
     *
     * @param btn Label for the Panel
     */
    void set_label(lv_obj_t *label);

public:
    /********************
     * Public Variables *
     ********************/
    Information *panel_info; // Information object associated with the Panel
    lv_obj_t *btn;           // Button in the Panel
    lv_obj_t *data;          // Data inside of and displayed on the Button
    lv_obj_t *label;         // Label underneath the Button that tells what Data is displayed

    /********************
     * Public Functions *
     ********************/

    /**
     * Default constructor for Panel objects. Creates a new Information
     * object using the default Information object constructor, and sets the Panel's
     * panel_info that that Information object.
     *
     * @note Please done use this. Kinda useless cuz you kinda need more information
     * in order to actually use this shit.
     */
    Panel();

    /**
     * Trying something new. Takes in only Information object and leaves it up to
     * Panel to initialize/store the button, data, and labels.
     *
     * @param info Pointer to an existing Information object
     */
    Panel(Information *info);

    /**
     * Main constructor for Panel objects. Creates an Information object
     * using the passed-in pointer, and sets the Panel's panel_info variable to
     * that Information object. Doesn't initialize any button or label styles.
     *
     * @param info Pointer to an existing Information object
     * @param btn Pointer to lv_obj_t for a button
     * @param data Pointer to lv_obj_t for button data
     * @param label Pointer to lv_obj_t for a label
     */
    Panel(Information *info, lv_obj_t *btn, lv_obj_t *data, lv_obj_t *label);
};

#endif