#ifndef CONFIGURATOR_H
#define CONFIGURATOR_H

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
     * Sets up the styles for all of the elements in a Configuration
     * object. These styles are for:
     * -> Drop-down menu
     * -> Radio buttons (both selected and deselected)
     * -> Done button
     */
    void style_init();

    /**
     * Sets up the layout, size, and properties of the window object.
     * Also adds all of the elements that go into the Configurator.
     */
    void init_window();

    /**
     * Sets up the layout structure of the Configurator window. This
     * utilizes the LVGL flex layout with 2 columns, allowing for easy
     * resizing of elements and easy layout creation.
     */
    void init_window_layout();

    /**
     * Initializes the drop-down menu in the Configurator.
     */
    void init_drop_down();

    /**
     * Creates the "warning" button and puts it into the correct spot on
     * the Configurator window.
     */
    void init_warning_btn();

    /**
     * Creates the "Done" button and puts it into the correct spot on
     * the Configurator window.
     */
    void init_done_btn();

    /**
     * Creates the radio button for selecing unit type in the Configurator
     * window.
     */
    void init_radio_btns();

    /**
     * Gets the associated Information object based on the option
     * selected in the drop-down menu.
     *
     * @note The drop-down object has a function "lv_dropdown_get_selected()"
     * which returns the index of the option that is currently selected.
     * Since we generate the options from an existing list, we can use this
     * index to figure out what Information object corresponds to the selected
     * option.
     *
     * @return the Information object that corresponds to the selected option in the drop-down
     */
    Information *get_info_from_drop_down();

    void warning_button_cb(lv_event_t *e);

    void open_warning_config();

public:
    /********************
     * Public Variables *
     ********************/

    lv_obj_t *window;                 // Window object that holds all other elements
    lv_obj_t *flex_col_1;             // Column 1 of flex layout
    lv_obj_t *flex_col_2;             // Column 2 of flex layout
    lv_obj_t *drop_down;              // Drop-down menu
    lv_obj_t *radio_imperial;         // Radio button for imperial units
    lv_obj_t *radio_metric;           // Radio button for metric units
    lv_obj_t *btn_warning;            // Button that opens warning configuration screen
    lv_obj_t *label_warning;          // Label for warning button
    lv_obj_t *btn_done;               // Done button
    lv_obj_t *label_done;             // Label for done button
    lv_style_t style_window;          // Style for window
    lv_style_t style_drop_down;       // Style for drop-down menu
    lv_style_t style_radio_checked;   // Style for radio button when selected
    lv_style_t style_radio_unchecked; // Style for radio button when deselected
    lv_style_t style_btn_done;        // Style for done button
    lv_style_t style_label_done;      // Style for the labe for the done button
    lv_style_t style_layout;          // Style for flex layout used
    Panel *panel_selected;            // Panel that the Configurator was opened by

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
     * Initializes the Configurator object. Called after lv_init().
     */
    void init_configurator();

    /**
     * Displays the Configurator window.
     */
    void open_window();

    /**
     * Called when "Done" button is pressed. *Should be called in button callback*
     * Will update the panel that was selected to open the Configurator window
     * with the Information object that was selected in the drop-down menu when
     * the "Done" button was pressed.
     */
    void close_window();

    /**
     * Selects the imperial radio button in the Configurator window and
     * deselects the metric radio button. Does this by setting the state
     * of the imperial radio button to LV_STATE_CHECKED, and clearing the
     * state of the metric radio button.
     */
    void select_imperial_radio_btn();

    /**
     * Selects the metric radio button in the Configurator window and
     * deselects the imperial radio button. Does this by setting the state
     * of the imperial metric button to LV_STATE_CHECKED, and clearing the
     * state of the imperial radio button.
     */
    void select_metric_radio_btn();

    /**
     * Set the callback of a given object, triggered on the given state.
     *
     * @param object pointer to a lv_obj_t that the callback will be attached to
     * @param function pointer to a callback function to be called
     * @param state enum for the state trigger that the callback will be called on
     * @param user_data custom data that will be available in the callback (can pass in NULL)
     */
    void set_callback(lv_obj_t *object, void (*function)(lv_event_t *e), lv_event_code_t state, void *user_data);
};

/***********************************
 * Configurator Object Declaration *
 ***********************************/

extern Configurator config;

#endif