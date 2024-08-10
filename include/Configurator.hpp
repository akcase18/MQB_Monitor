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

public:
    /********************
     * Public Variables *
     ********************/

    static lv_obj_t *window;
    static lv_obj_t *drop_down;
    static lv_obj_t *radio_imperial;
    static lv_obj_t *radio_metric;
    static lv_obj_t *btn_done;
    static lv_style_t *style_window;
    static lv_style_t *style_drop_down;
    static lv_style_t *style_radio_checked;
    static lv_style_t *style_radio_unchecked;
    static lv_style_t *style_btn_done;

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
     * Creates and displays the Configurator window.
    */
    void show_config_window();
};

#endif