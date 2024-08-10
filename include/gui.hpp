#include "can_handler.hpp"

/********************
 * Global Constants *
 ********************/

#define LCD_HORZ 480 // Horizontal resolution of the display in pixels
#define LCD_VERT 320 // Vertical resolution of the display in pixels

/*******************
 * Button Creation *
 *******************/

lv_obj_t *btn_tl; // Top left button object
lv_obj_t *btn_tc; // Top center button object
lv_obj_t *btn_tr; // Top right button object
lv_obj_t *btn_bl; // Bottom left button object
lv_obj_t *btn_br; // Bottom right button objet

/************************
 * Button Data Creation *
 ************************/

lv_obj_t *data_tl; // Top left data object
lv_obj_t *data_tc; // Top center data object
lv_obj_t *data_tr; // Top right data object
lv_obj_t *data_bl; // Bottom left data object
lv_obj_t *data_br; // Bottom right data object

/******************
 * Label Creation *
 ******************/

lv_obj_t *label_tl; // Top left label object
lv_obj_t *label_tc; // Top center label object
lv_obj_t *label_tr; // Top right label object
lv_obj_t *label_bl; // Bottom left label object
lv_obj_t *label_br; // Bottom right label object

/******************
 * Style Creation *
 ******************/

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
static lv_style_t style_radio;     // Radio button style (unchecked)
static lv_style_t style_radio_chk; // Radio button style (checked)

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
}

static void config_window_create(void)
{
}

/*******************
 * Object Creation *
 *******************/

static void add_main_objects(void)
{
}

static void add_main_labels(void)
{
}

static void add_config_objects(void)
{
}

static void radiobutton_create(lv_obj_t *parent, lv_obj_t *child, const char *text)
{
}

/*************
 * Callbacks *
 *************/

static void button_config_cb(lv_event_t *e)
{
}

static void close_window_cb(lv_event_t *e)
{
}

static void radio_cb(lv_event_t *e)
{
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

/******************
 * Update Display *
 ******************/

void top_left_timer_cb(lv_timer_t *timer)
{
    
}

void top_center_timer_cb(lv_timer_t *timer)
{
}

void top_right_timer_cb(lv_timer_t *timer)
{
}

void bottom_left_timer_cb(lv_timer_t *timer)
{
}

void bottom_right_timer_cb(lv_timer_t *timer)
{
}