#ifndef PANEL_H
#define PANEL_H

/**
 * This is the Panel class.
 *
 * The Panel class is used hold a variety of different things, organized
 * in a way that allows for more modular use-cases. Each Panel object can
 * hold:
 * -> An Information object
 * -> A button
 * -> Data to be displayed in the button
 * -> A label for the button
 * -> Style types for both the button and label
 * The idea behind Panel objects is to be used in multiple different Layouts,
 * where Panels can be rearranged depending on a variety of factors.
 * All elements of each Panel object can be reconfigured as needed as
 * different things change or different Configurations are chosen.
 *
 * Used in:
 * -> Layout
 */

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

    Information *panel_info; // The Information object for this Panel
    lv_obj_t *btn;           // The button this Panel handles
    lv_obj_t *data_btn;      // The data that goes inside of the button
    lv_obj_t *label;         // The label associated with the button
    lv_style_t *style_btn;   // The style of the button
    lv_style_t *style_label; // The style of the panel

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

    /**
     * Sets the styles of both the button and label of the panel
     * 
     * @param btn_style the style of the button
     * @param label_style the style of the label
     */
    void set_styles(lv_style_t *btn_style, lv_style_t *label_style);

    void set_button_style(lv_style_t *style);
    lv_style_t *get_button_style();

    void set_label_style(lv_style_t *style);
    lv_style_t *get_label_style();
};

#endif