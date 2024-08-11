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
#include "fonts.hpp"

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

public:
    /********************
     * Public Variables *
     ********************/

    static lv_obj_t *window;                  // Window object that holds all other elements
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
};

#endif