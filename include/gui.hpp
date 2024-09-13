#include <lvgl.h>
// #include <Arduino.h>
#include "can_logic.hpp"

/***********
 * Defines *
 ***********/

#define LCD_HORZ 480
#define LCD_VERT 320

/****************
 * Enumerations *
 ****************/

enum info_select
{
    coolant_temp_enum,
    engine_oil_temp_enum,
    intake_air_temp_enum,
    charge_pressure_enum,
    intake_pressure_enum,
    ambient_pressure_enum,
    lambda_actual_enum,
    lambda_specified_enum,
    turbo_speed_enum,
    engine_speed_enum,
    engine_torque_enum,
    vehicle_speed_enum,
    fuel_level_enum,
    fuel_percent_enum,
    throttle_position_enum,
    misfire_count_enum,
    misfire_cyl_1_enum,
    misfire_cyl_2_enum,
    misfire_cyl_3_enum,
    misfire_cyl_4_enum,
    timing_corr_1_enum,
    timing_corr_2_enum,
    timing_corr_3_enum,
    timing_corr_4_enum
};

/**********
 * Styles *
 **********/

/* Window Styles */
static lv_style_t style_main;   // Main window style
static lv_style_t style_config; // Config window style
static lv_style_t style_flex;   // Flex layout style
/* Button Styles */
static lv_style_t style_btn_top;     // Top row button style
static lv_style_t style_btn_btm;     // Bottom row button style
static lv_style_t style_btn_pressed; // Button style when pressed
static lv_style_t style_btn_done;    // Done button style
/* Label Styles */
static lv_style_t style_label_top; // Top row label style
static lv_style_t style_label_btm; // Bottom row label style
/* Radio Button Styles */
static lv_style_t style_radio;
static lv_style_t style_radio_chk;

/***********
 * Objects *
 ***********/

/* Window Objects */
static lv_obj_t *main_window;   // Main window object
static lv_obj_t *config_window; // Config window object
/* Button Objects */
static lv_obj_t *top_left;     // Top left button object
static lv_obj_t *top_center;   // Top center button object
static lv_obj_t *top_right;    // Top right button object
static lv_obj_t *bottom_left;  // Bottom left button object
static lv_obj_t *bottom_right; // Buttom right button object
static lv_obj_t *done_btn;     // Done button for config window
/* Button Data Objects */
static lv_obj_t *top_left_data;     // Top left data object
static lv_obj_t *top_center_data;   // Top center data object
static lv_obj_t *top_right_data;    // Top right data object
static lv_obj_t *bottom_left_data;  // Bottom left data object
static lv_obj_t *bottom_right_data; // Bottom right data object
/* Label Objects */
static lv_obj_t *top_left_label;     // Top left label object
static lv_obj_t *top_center_label;   // Top center label object
static lv_obj_t *top_right_label;    // Top right label object
static lv_obj_t *bottom_left_label;  // Bottom left label object
static lv_obj_t *bottom_right_label; // Bottom right label object
static lv_obj_t *done_label;         // Done label for config window
/* Flex Objects */
static lv_obj_t *flex_col_1;
static lv_obj_t *flex_col_2;
/* Drop-Down Objects */
static lv_obj_t *msg_drop_down; //
/* Radio Button Objects */
static lv_obj_t *radio_imperial; //
static lv_obj_t *radio_metric;   //
/* Currently Selected */
static lv_obj_t *curr_select_panel; // Currently selected button object
// static const char *curr_select_label; // Current selected label string
uint8_t curr_select_object;

/*********
 * Fonts *
 *********/

/* Data */
LV_FONT_DECLARE(digital_display_80);
LV_FONT_DECLARE(digital_display_90);
LV_FONT_DECLARE(digital_display_100);
LV_FONT_DECLARE(digital_display_115);
LV_FONT_DECLARE(digital_display_120);
LV_FONT_DECLARE(digital_display_130);
LV_FONT_DECLARE(digital_display_135);
LV_FONT_DECLARE(digital_display_140);
/* Labels */
LV_FONT_DECLARE(vw_14);
LV_FONT_DECLARE(vw_16);
LV_FONT_DECLARE(vw_18);
LV_FONT_DECLARE(vw_24);
/* Font Objects */
const lv_font_t *top_data_font = &digital_display_100;
const lv_font_t *btm_data_font = &digital_display_140;
const lv_font_t *top_label_font = &vw_16;
const lv_font_t *btm_label_font = &vw_18;
const lv_font_t *done_label_font = &vw_24;
const lv_font_t *drop_down_font = &vw_24;

/********
 * Grid *
 ********/

static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
static lv_coord_t row_dsc[] = {LV_GRID_FR(6), LV_GRID_FR(1), LV_GRID_FR(8), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

/***********
 * Display *
 ***********/

static lv_display_t *display;
static lv_color_t buf_1[(LCD_HORZ * LCD_VERT) / 10];
static lv_color_t buf_2[(LCD_HORZ * LCD_VERT) / 10];

/*******************
 * Touch Interface *
 *******************/

lv_indev_t *touch_indev;

/****************
 * CAN Messages *
 ****************/

/***** Performance Metrics *****/
Information coolant_temp = Information("Coolant Temp", 0xF4, 0x05, -40, NO_SCALE, Information::TEMP, 0, 0, 2500);
Information engine_oil_temp = Information("Engine Oil Temp", 0x20, 0x2F, -2731.4, 0.1, Information::TEMP, 0, 0, 1000);
Information intake_air_temp = Information("Intake Air Temp", 0xF4, 0x0F, -40, NO_SCALE, Information::TEMP, 0, 0, 100);
Information charge_pressure = Information("Charge Pressure", 0x20, 0x2A, NO_OFFSET, 0.001, Information::PRESSURE, 2, 1, 100);
Information intake_pressure = Information("Intake Pressure", 0x3E, 0x70, NO_OFFSET, 0.001, Information::PRESSURE, 2, 1, MAX_REFRESH);
Information ambient_pressure = Information("Ambient Pressure", 0x13, 0xCA, NO_OFFSET, 0.001, Information::PRESSURE, 2, 1, 1000);
Information lambda_actual = Information("Lambda (actual)", 0x10, 0xC0, NO_OFFSET, (1 / 1024), Information::NONE, 1, 1, MAX_REFRESH);
Information lambda_specified = Information("Lambda (specified)", 0x29, 0x50, NO_OFFSET, (1 / 1024), Information::NONE, 1, 1, MAX_REFRESH);
Information turbo_speed = Information("Turbo Speed", 0x10, 0x40, NO_OFFSET, 6.11, Information::RPM, 0, 0, MIN_REFRESH);

/***** Car Information *****/
Information engine_rpm = Information("Engine Speed", 0x20, 0x6E, NO_OFFSET, NO_SCALE, Information::RPM, 0, 0, MAX_REFRESH);
Information engine_torque = Information("Engine Torque", 0x43, 0x7C, NO_OFFSET, 0.1, Information::TORQUE, 0, 0, MAX_REFRESH);
Information vehicle_speed = Information("Vehicle Speed", 0x20, 0x33, NO_OFFSET, 0.01, Information::SPEED, 0, 0, 100);
Information fuel_level = Information("Fuel", 0x22, 0x06, NO_OFFSET, NO_SCALE, Information::VOLUME, 1, 1, MIN_REFRESH);
Information fuel_percent = Information("Fuel", 0xF4, 0x2F, NO_OFFSET, (1 / 2.55), Information::PERCENT, 0, 0, MIN_REFRESH);
Information throttle_position = Information("Throttle Position", 0xF4, 0x11, NO_OFFSET, (1 / 2.55), Information::PERCENT, 0, 0, MAX_REFRESH);

/***** Diag/Monitoring *****/
Information misfire_sum = Information("Misfire Count", 0x29, 0x04, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
Information misfire_cyl_1 = Information("Misfire Count Cyl 1", 0x29, 0x66, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
Information misfire_cyl_2 = Information("Misfire Count Cyl 2", 0x29, 0x67, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
Information misfire_cyl_3 = Information("Misfire Count Cyl 3", 0x29, 0x68, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
Information misfire_cyl_4 = Information("Misfire Count Cyl 4", 0x29, 0x69, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
Information timing_corr_1 = Information("Timing Corr. Cyl 1", 0x20, 0x0A, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);
Information timing_corr_2 = Information("Timing Corr. Cyl 2", 0x20, 0x0B, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);
Information timing_corr_3 = Information("Timing Corr. Cyl 3", 0x20, 0x0C, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);
Information timing_corr_4 = Information("Timing Corr. Cyl 4", 0x20, 0x0D, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);

/**********
 * Panels *
 **********/

static Information top_left_panel = Information(coolant_temp);          // Information object for the top left panel
static Information top_center_panel = Information(engine_oil_temp);     // Information object for the top center panel
static Information top_right_panel = Information(intake_air_temp);      // Information object for the top right panel
static Information bottom_left_panel = Information(intake_pressure);    // Information object for the bottom left panel
static Information bottom_right_panel = Information(throttle_position); // Information object for the bottom right panel

/*******************
 * Variable Status *
 *******************/

static bool imperial = true;      // Whether the unit radio button selected imperial or not
static uint32_t active_index = 0; // Used to keep track of the active object in the configuration window

/********************
 * Static Functions *
 ********************/

/***** Initialization *****/

/**
 * Initialize all styles for objects and labels
 */
static void style_init(void);

/***** Window Creation *****/

/**
 * Create the main layout
 *
 * Consists of:
 *
 * 5 buttons (3 small on top row, 2 large on bottom row)
 * Corresponding data labels
 * Corresponding button labels
 */
static void main_window_create(void);
/**
 * Create the layout for the configuration screen
 *
 * Consists of:
 *
 * Dropdown menu to select information displayed
 * Radio button to select unit for information
 * Done button
 */
static void config_window_create(void);

/***** Object Creation *****/

/**
 * Add objects (buttons) to the main window
 */
static void add_main_objects(void);
/**
 * Adds labels to buttons in main window
 */
static void add_main_labels(void);
/**
 * Add objects (dropdown menu, unit select button, done button) to the
 * configuration window
 */
static void add_config_objects(void);
/**
 * Create a radio button
 */
static void radiobutton_create(lv_obj_t *parent, lv_obj_t *child, const char *text);

/***** Callbacks *****/

/**
 * Open the configuration window
 */
static void button_config_cb(lv_event_t *e);
/**
 * Close the configuration window
 */
static void close_window_cb(lv_event_t *e);
/**
 * Switch the state of the radio button and store that data
 */
static void radio_cb(lv_event_t *e);

/***** Transforms *****/

/**
 * Darken the button by the specified opacity
 */
static lv_color_t darken(const lv_color_filter_dsc_t *dsc, lv_color_t color, lv_opa_t opa);

/***** Helpers *****/

/**
 * Gets the state of the dropdown menu in the configuration window
 * and sets the label to the correct value based on the selection
 */
static uint8_t get_config_dropdown_state(lv_obj_t *dropdown);

/***** Timer Callbacks *****/

void top_left_timer_cb(lv_timer_t *timer);
void top_center_timer_cb(lv_timer_t *timer);
void top_right_timer_cb(lv_timer_t *timer);
void bottom_left_timer_cb(lv_timer_t *timer);
void bottom_right_timer_cb(lv_timer_t *timer);

/**
 * Update the data in each button
 */
void update_buttons();

/******************
 * Initialization *
 ******************/

static void style_init(void)
{
    /* Set the style of the main window */
    lv_style_init(&style_main);
    lv_style_set_bg_color(&style_main, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_border_width(&style_main, 0);
    lv_style_set_radius(&style_main, 0);

    /* Set the style of the configuration window */
    lv_style_init(&style_config);
    lv_style_set_bg_color(&style_config, lv_palette_darken(LV_PALETTE_GREY, 4));

    /* Set the style of the top data buttons */
    lv_style_init(&style_btn_top);
    lv_style_set_radius(&style_btn_top, 2);
    lv_style_set_opa(&style_btn_top, LV_OPA_100);
    lv_style_set_bg_color(&style_btn_top, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_text_color(&style_btn_top, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_text_font(&style_btn_top, top_data_font);
    lv_style_set_text_letter_space(&style_btn_top, 0);
    lv_style_set_shadow_width(&style_btn_top, 0);

    /* Set the style of the bottom data buttons */
    lv_style_init(&style_btn_btm);
    lv_style_set_radius(&style_btn_btm, 2);
    lv_style_set_opa(&style_btn_btm, LV_OPA_100);
    lv_style_set_bg_color(&style_btn_btm, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_text_color(&style_btn_btm, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_text_font(&style_btn_btm, btm_data_font);
    lv_style_set_text_letter_space(&style_btn_btm, 0);
    lv_style_set_shadow_width(&style_btn_btm, 0);

    /* Set the style of the "done" button */
    lv_style_init(&style_btn_done);
    lv_style_set_radius(&style_btn_done, 5);
    lv_style_set_opa(&style_btn_done, LV_OPA_100);
    lv_style_set_bg_color(&style_btn_done, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_text_color(&style_btn_done, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_text_font(&style_btn_done, done_label_font);
    lv_style_set_text_letter_space(&style_btn_done, 0);
    lv_style_set_shadow_width(&style_btn_done, 0);

    /* Darken button when pressed */
    static lv_color_filter_dsc_t color_filter_btn;
    lv_color_filter_dsc_init(&color_filter_btn, darken);
    lv_style_init(&style_btn_pressed);
    lv_style_set_color_filter_dsc(&style_btn_pressed, &color_filter_btn);
    lv_style_set_color_filter_opa(&style_btn_pressed, LV_OPA_20);

    /* Set the style of the top row label object */
    lv_style_init(&style_label_top);
    lv_style_set_text_color(&style_label_top, lv_palette_lighten(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(&style_label_top, top_label_font);
    lv_style_set_text_letter_space(&style_label_top, 0);

    /* Set the style of the bottom row label object */
    lv_style_init(&style_label_btm);
    lv_style_set_text_color(&style_label_btm, lv_palette_lighten(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(&style_label_btm, btm_label_font);
    lv_style_set_text_letter_space(&style_label_btm, 0);

    /* Set the style of the radio button for selecting unit region */
    lv_style_init(&style_radio);
    lv_style_set_radius(&style_radio, LV_RADIUS_CIRCLE);
    lv_style_set_text_color(&style_radio, lv_palette_lighten(LV_PALETTE_GREY, 5));
    /* Set the style of the radio button when checked */
    lv_style_init(&style_radio_chk);
    lv_style_set_bg_img_src(&style_radio_chk, NULL);

    /* Set the style for the flex layout objects */
    lv_style_init(&style_flex);
    lv_style_set_bg_color(&style_flex, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_border_width(&style_flex, 0);
    lv_style_set_text_color(&style_flex, lv_palette_lighten(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(&style_flex, drop_down_font);
}

/*******************
 * Window Creation *
 *******************/

static void main_window_create(void)
{
    main_window = lv_obj_create(lv_scr_act());
    lv_obj_set_size(main_window, 480, 320);
    lv_obj_center(main_window);
    lv_obj_add_style(main_window, &style_main, 0);
    /* Set grid layout type */
    lv_obj_set_grid_dsc_array(main_window, col_dsc, row_dsc);
    /* Remove padding around all objects to fit them closer together */
    lv_obj_set_style_pad_all(main_window, 0, 0);    // Remove padding around border of display
    lv_obj_set_style_pad_column(main_window, 0, 0); // Remove padding from columns between objects
    lv_obj_set_style_pad_row(main_window, 0, 0);    // Remove padding from rows between objects
    /* Turn off scrolling */
    lv_obj_set_scrollbar_mode(main_window, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(main_window, LV_OBJ_FLAG_SCROLLABLE);
    /* Add objects */
    add_main_objects();
    add_main_labels();
}

static void config_window_create(void)
{
    config_window = lv_obj_create(lv_scr_act());
    lv_obj_set_size(config_window, 480, 320);
    lv_obj_center(config_window);
    lv_obj_add_style(config_window, &style_config, 0);
    /* Turn off scrolling */
    lv_obj_set_scrollbar_mode(config_window, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(config_window, LV_OBJ_FLAG_SCROLLABLE);
    /* Add objects */
    add_config_objects();
}

/*******************
 * Object Creation *
 *******************/

static void add_main_objects(void)
{
    top_left = lv_btn_create(main_window);
    lv_obj_add_style(top_left, &style_btn_top, 0);
    lv_obj_add_style(top_left, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(top_left, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_add_event_cb(top_left, button_config_cb, LV_EVENT_CLICKED, top_left);
    top_left_data = lv_label_create(top_left);
    lv_label_set_text(top_left_data, top_left_panel.get_data_str().c_str());
    lv_obj_align(top_left_data, LV_ALIGN_CENTER, 0, 0);

    top_center = lv_btn_create(main_window);
    lv_obj_add_style(top_center, &style_btn_top, 0);
    lv_obj_add_style(top_center, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(top_center, LV_GRID_ALIGN_STRETCH, 2, 2, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_add_event_cb(top_center, button_config_cb, LV_EVENT_CLICKED, top_center);
    top_center_data = lv_label_create(top_center);
    lv_label_set_text(top_center_data, top_center_panel.get_data_str().c_str());
    lv_obj_align(top_center_data, LV_ALIGN_CENTER, 0, 0);

    top_right = lv_btn_create(main_window);
    lv_obj_add_style(top_right, &style_btn_top, 0);
    lv_obj_add_style(top_right, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(top_right, LV_GRID_ALIGN_STRETCH, 4, 2, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_add_event_cb(top_right, button_config_cb, LV_EVENT_CLICKED, top_right);
    top_right_data = lv_label_create(top_right);
    lv_label_set_text(top_right_data, top_right_panel.get_data_str().c_str());
    lv_obj_align(top_right_data, LV_ALIGN_CENTER, 0, 0);

    bottom_left = lv_btn_create(main_window);
    lv_obj_add_style(bottom_left, &style_btn_btm, 0);
    lv_obj_add_style(bottom_left, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(bottom_left, LV_GRID_ALIGN_STRETCH, 0, 3, LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_add_event_cb(bottom_left, button_config_cb, LV_EVENT_CLICKED, bottom_left);
    bottom_left_data = lv_label_create(bottom_left);
    lv_label_set_text(bottom_left_data, bottom_left_panel.get_data_str().c_str());
    lv_obj_align(bottom_left_data, LV_ALIGN_CENTER, 0, 0);

    bottom_right = lv_btn_create(main_window);
    lv_obj_add_style(bottom_right, &style_btn_btm, 0);
    lv_obj_add_style(bottom_right, &style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(bottom_right, LV_GRID_ALIGN_STRETCH, 3, 3, LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_add_event_cb(bottom_right, button_config_cb, LV_EVENT_CLICKED, bottom_right);
    bottom_right_data = lv_label_create(bottom_right);
    lv_label_set_text(bottom_right_data, bottom_right_panel.get_data_str().c_str());
    lv_obj_align(bottom_right_data, LV_ALIGN_CENTER, 0, 0);
}

static void add_main_labels(void)
{
    top_left_label = lv_label_create(main_window);
    lv_label_set_text(top_left_label, top_left_panel.get_name().c_str());
    lv_obj_add_style(top_left_label, &style_label_top, 0);
    lv_obj_set_grid_cell(top_left_label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1);

    top_center_label = lv_label_create(main_window);
    lv_label_set_text(top_center_label, top_center_panel.get_name().c_str());
    lv_obj_add_style(top_center_label, &style_label_top, 0);
    lv_obj_set_grid_cell(top_center_label, LV_GRID_ALIGN_CENTER, 2, 2, LV_GRID_ALIGN_CENTER, 1, 1);

    top_right_label = lv_label_create(main_window);
    lv_label_set_text(top_right_label, top_right_panel.get_name().c_str());
    lv_obj_add_style(top_right_label, &style_label_top, 0);
    lv_obj_set_grid_cell(top_right_label, LV_GRID_ALIGN_CENTER, 4, 2, LV_GRID_ALIGN_CENTER, 1, 1);

    bottom_left_label = lv_label_create(main_window);
    lv_label_set_text(bottom_left_label, bottom_left_panel.get_name().c_str());
    lv_obj_add_style(bottom_left_label, &style_label_btm, 0);
    lv_obj_set_grid_cell(bottom_left_label, LV_GRID_ALIGN_CENTER, 0, 3, LV_GRID_ALIGN_CENTER, 3, 1);

    bottom_right_label = lv_label_create(main_window);
    lv_label_set_text(bottom_right_label, bottom_right_panel.get_name().c_str());
    lv_obj_add_style(bottom_right_label, &style_label_btm, 0);
    lv_obj_set_grid_cell(bottom_right_label, LV_GRID_ALIGN_CENTER, 3, 3, LV_GRID_ALIGN_CENTER, 3, 1);
}

static void add_config_objects(void)
{
    /* Create layout */
    flex_col_1 = lv_obj_create(config_window);
    lv_obj_set_flex_flow(flex_col_1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(flex_col_1, lv_pct(60), lv_pct(100));
    lv_obj_add_style(flex_col_1, &style_flex, 0);
    flex_col_2 = lv_obj_create(config_window);
    lv_obj_set_flex_flow(flex_col_2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(flex_col_2, lv_pct(40), lv_pct(100));
    lv_obj_add_event_cb(flex_col_2, radio_cb, LV_EVENT_CLICKED, &active_index);
    lv_obj_set_x(flex_col_2, lv_pct(60));
    lv_obj_add_style(flex_col_2, &style_flex, 0);
    /* Create dropdown menu for selecting data for panel */
    msg_drop_down = lv_dropdown_create(flex_col_1);
    lv_dropdown_set_options(msg_drop_down, "Coolant Temp\n"
                                           "Engine Oil Temp\n"
                                           "Intake Air Temp\n"
                                           "Charge Pressure\n"
                                           "Intake Pressure\n"
                                           "Ambient Pressure\n"
                                           "Lambda (actual)\n"
                                           "Lambda (specified)\n"
                                           "Turbo Speed\n"
                                           "Engine Speed\n"
                                           "Engine Torque\n"
                                           "Vehicle Speed\n"
                                           "Fuel (level)\n"
                                           "Fuel (percent)\n"
                                           "Throttle Position\n"
                                           "Misfire Count\n"
                                           "Misfire Count Cyl 1\n"
                                           "Misfire Count Cyl 2\n"
                                           "Misfire Count Cyl 3\n"
                                           "Misfire Count Cyl 4\n"
                                           "Timing Corr Cyl 1\n"
                                           "Timing Corr Cyl 2\n"
                                           "Timing Corr Cyl 3\n"
                                           "Timing Corr Cyl 4");
    lv_obj_align(msg_drop_down, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_size(msg_drop_down, lv_pct(100), 55);
    /* Create radio button for selecting Imperial or Metric */
    radiobutton_create(flex_col_2, radio_imperial, "Imperial");
    radiobutton_create(flex_col_2, radio_metric, "Metric");
    if (imperial)
    {
        lv_obj_add_state(lv_obj_get_child(flex_col_2, 0), LV_STATE_CHECKED);
        lv_obj_clear_state(lv_obj_get_child(flex_col_2, 1), LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_add_state(lv_obj_get_child(flex_col_2, 1), LV_STATE_CHECKED);
        lv_obj_clear_state(lv_obj_get_child(flex_col_2, 0), LV_STATE_CHECKED);
    }
    /* Create close button */
    done_btn = lv_obj_create(config_window);
    lv_obj_set_size(done_btn, 90, 50);

    /* Turn off scrolling */
    lv_obj_set_scrollbar_mode(done_btn, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(done_btn, LV_OBJ_FLAG_SCROLLABLE);
    /* Add "Done" text */
    done_label = lv_label_create(done_btn);
    lv_label_set_text(done_label, "Done");
    lv_obj_align(done_label, LV_ALIGN_CENTER, 0, 0);
}

static void radiobutton_create(lv_obj_t *parent, lv_obj_t *child, const char *text)
{
    child = lv_checkbox_create(parent);
    lv_checkbox_set_text(child, text);
    lv_obj_add_flag(child, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_style(child, &style_radio, LV_PART_INDICATOR);
    lv_obj_add_style(child, &style_radio_chk, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (child == radio_imperial)
    {
        lv_obj_align(child, LV_ALIGN_TOP_MID, lv_pct(0), lv_pct(0));
    }
    else
    {
        lv_obj_align(child, LV_ALIGN_TOP_MID, lv_pct(0), lv_pct(25));
    }
}

/*************
 * Callbacks *
 *************/

static void button_config_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        curr_select_panel = (lv_obj_t *)lv_event_get_target(e);
        curr_select_object = lv_obj_get_index(curr_select_panel);
        config_window_create();
        // top_left_panel.set_data(curr_select_object);
    }
}

static void close_window_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        Information info;
        uint8_t info_type = get_config_dropdown_state(msg_drop_down);
        switch (info_type)
        {
        case info_select::coolant_temp_enum:
            info = Information(coolant_temp);
            break;

        case info_select::engine_oil_temp_enum:
            info = Information(engine_oil_temp);
            break;

        case info_select::intake_air_temp_enum:
            info = Information(intake_air_temp);
            break;

        case info_select::charge_pressure_enum:
            info = Information(charge_pressure);
            break;

        case info_select::intake_pressure_enum:
            info = Information(intake_pressure);
            break;

        case info_select::ambient_pressure_enum:
            info = Information(ambient_pressure);
            break;

        case info_select::lambda_actual_enum:
            info = Information(lambda_actual);
            break;

        case info_select::lambda_specified_enum:
            info = Information(lambda_specified);
            break;

        case info_select::turbo_speed_enum:
            info = Information(turbo_speed);
            break;

        case info_select::engine_speed_enum:
            info = Information(engine_rpm);
            break;

        case info_select::engine_torque_enum:
            info = Information(engine_torque);
            break;

        case info_select::vehicle_speed_enum:
            info = Information(vehicle_speed);
            break;

        case info_select::fuel_level_enum:
            info = Information(fuel_level);
            break;

        case info_select::fuel_percent_enum:
            info = Information(fuel_percent);
            break;

        case info_select::throttle_position_enum:
            info = Information(throttle_position);
            break;

        case info_select::misfire_count_enum:
            info = Information(misfire_sum);
            break;

        case info_select::misfire_cyl_1_enum:
            info = Information(misfire_cyl_1);
            break;

        case info_select::misfire_cyl_2_enum:
            info = Information(misfire_cyl_2);
            break;

        case info_select::misfire_cyl_3_enum:
            info = Information(misfire_cyl_3);
            break;

        case info_select::misfire_cyl_4_enum:
            info = Information(misfire_cyl_4);
            break;

        case info_select::timing_corr_1_enum:
            info = Information(timing_corr_1);
            break;

        case info_select::timing_corr_2_enum:
            info = Information(timing_corr_2);
            break;

        case info_select::timing_corr_3_enum:
            info = Information(timing_corr_3);
            break;

        case info_select::timing_corr_4_enum:
            info = Information(timing_corr_4);
            break;

        default:
            info = Information(coolant_temp);
            break;
        }
        // Have Information object get set here
        // Information top_left = dropdown_selected
        switch (curr_select_object)
        {
        case 0:
            top_left_panel = info;
            break;

        case 1:
            top_center_panel = info;
            break;

        case 2:
            top_right_panel = info;
            break;

        case 3:
            bottom_left_panel = info;
            break;

        case 4:
            bottom_right_panel = info;
            break;
        
        default:
            top_left_panel = info;
            break;
        }
        main_window_create();
    }
}

static void radio_cb(lv_event_t *e)
{
    uint32_t *active_id = (uint32_t *)lv_event_get_user_data(e);
    lv_obj_t *cont = (lv_obj_t *)lv_event_get_current_target(e);
    lv_obj_t *act_cb = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *old_cb = lv_obj_get_child(cont, *active_id);

    /* Do nothing if the container was clicked */
    if (act_cb == cont)
        return;

    lv_obj_clear_state(old_cb, LV_STATE_CHECKED); /* Uncheck the previous radio button */
    lv_obj_add_state(act_cb, LV_STATE_CHECKED);   /* Uncheck the current radio button */

    if (act_cb == lv_obj_get_child(flex_col_2, 0))
    {
        imperial = true;
    }
    else if (act_cb == lv_obj_get_child(flex_col_2, 1))
    {
        imperial = false;
    }

    *active_id = lv_obj_get_index(act_cb);
}

/**************
 * Transforms *
 **************/

static lv_color_t darken(const lv_color_filter_dsc_t *dsc, lv_color_t color, lv_opa_t opa)
{
    LV_UNUSED(dsc);
    return lv_color_darken(color, opa);
}

/***********
 * Helpers *
 ***********/

static uint8_t get_config_dropdown_state(lv_obj_t *dropdown)
{
    
}