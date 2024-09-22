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

    /**
     * Sets the style of the button in the Panel.
     *
     * @param style_btn Style of the button
     */
    void set_btn_style(lv_style_t style_btn);

    /**
     * Sets the style of the button in the Panel when it is pressed.
     *
     * @param style_btn_pressed Style of the button when pressed
     */
    void set_btn_pressed_style(lv_style_t style_btn_pressed);

    /**
     * Sets the style of the label in the Panel.
     *
     * @param style_label Style of the label
     */
    void set_label_style(lv_style_t style_label);

public:
    /********************
     * Public Variables *
     ********************/
    Information *panel_info; // Information object associated with the Panel
    lv_obj_t *btn;           // Button in the Panel
    lv_obj_t *data;          // Data inside of and displayed on the Button
    lv_obj_t *label;         // Label underneath the Button that tells what Data is displayed
    lv_style_t style_btn;         // Style for the Button
    lv_style_t style_btn_pressed; // Style for the Button when it is pressed
    lv_style_t style_label;       // Style for the Label

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

    /**
     * Main constructor for Panel objects. Creates an Information object
     * using the passed-in pointer, and sets the Panel's panel_info variable to
     * that Information object. Initializes button and label styles.
     *
     * @param info Pointer to an existing Information object
     * @param btn Pointer to lv_obj_t for a button
     * @param data Pointer to lv_obj_t for button data
     * @param label Pointer to lv_obj_t for a label
     * @param style_btn Style for button
     * @param style_btn_pressed Style for when button is pressed
     * @param style_label Style for label
     */
    Panel(Information *info, lv_obj_t *btn, lv_obj_t *data, lv_obj_t *label, lv_style_t style_btn, lv_style_t style_btn_pressed, lv_style_t style_label);

    /**
     * Sets the style of the button and label of the Panel after it has been
     * initialized.
     *
     * @param style_btn style of the button
     * @param style_btn_pressed style of the button when pressed
     * @param style_label style of the label
     */
    void set_styles(lv_style_t style_btn, lv_style_t style_btn_pressed, lv_style_t style_label);
};

#endif