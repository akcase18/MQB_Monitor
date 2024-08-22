#ifndef CONFIGURATOR_H
#define CONFIGURATOR_H

/**
 * This is the Configurator class.
 *
 * The Configurator class is almost like a different type of Layout.
 * While the Layout class carries a bunch of different Panels that hold
 * very specific things, the Configurator is basically a very specific
 * Layout that is used to modify the Panels that are held within the
 * main Layout object. The Configurator class holds:
 * -> A drop-down manu (for selecting Information objects)
 * -> A radio button for selecting unit "region"
 * -> A "Done" button
 * -> Style types for all other elements
 *
 * Used in:
 * -> CAN Handler
 * -> Other stuff above this
 */

#include "Layout.hpp"

using namespace std;

class Configurator
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
     * Creates the styles for all of the elements in a Configuration
     * object. These styles are for:
     * -> Drop-down menu
     * -> Radio buttons (both selected and deselected)
     * -> Done button
     */
    void config_style_init();

    /**
     * Sets up the layout, size, and properties of the window object
     */
    void setup_config_window();

    /**
     * Creates the list of options for the drop-down menu based on the
     * list of Information objects
     */
    void create_drop_down_list();

    /**
     * Creates the "Done" button and puts it into the correct spot on
     * the Configurator window
     */
    void create_done_btn();

    /**
     * Creates the radio button for selecing unit type in the Configurator
     * window
     */
    void create_radio_btns();

    /**
     * Gets the associated Information object based on the option
     * selected in the drop-down menu
     */
    Information *get_info_from_drop_down(lv_obj_t *drop_down);

    /**
     * Callback to service closing the Configurator object
     */
    static void close_window_cb(lv_event_t *e);

public:
    /********************
     * Public Variables *
     ********************/

    static lv_obj_t *window;                  // Window object that holds all other elements
    static lv_obj_t *flex_col_1;              // Column 1 of flex layout
    static lv_obj_t *flex_col_2;              // Column 2 of flex layout
    static lv_obj_t *drop_down;               // Drop-down menu
    static lv_obj_t *radio_imperial;          // Radio button for imperial units
    static lv_obj_t *radio_metric;            // Radio button for metric units
    static lv_obj_t *btn_done;                // Done button
    static lv_style_t *style_window;          // Style for window
    static lv_style_t *style_drop_down;       // Style for drop-down menu
    static lv_style_t *style_radio_checked;   // Style for radio button when selected
    static lv_style_t *style_radio_unchecked; // Style for radio button when deselected
    static lv_style_t *style_btn_done;        // Style for done button
    static lv_style_t *style_layout;          // Style for flex layout used
    Panel *panel_selected;                    // Panel that the Configurator was opened by
    vector<String> drop_down_list;            // Array of options for the drop-down menu

    /********************
     * Public Functions *
     ********************/

    /**
     * Default constructor for Configurator objects. Since the Configurator
     * object is constant and unchanging, there is no need to pass in any
     * elements to the Constructor. This Constructor will assign everything
     * that is needed properly.
     */
    Configurator();

    /**
     * Displays the Configurator window.
     */
    void show_config_window();

    /**
     * Called when "Done" button is pressed. *Should be called in button callback*
     * Will update the panel that was selected to open the Configurator window
     * with the Information object that was selected in the drop-down menu when
     * the "Done" button was pressed.
     */
    void close_config_window();
};

#endif