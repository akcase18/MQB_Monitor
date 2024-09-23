// #include <Arduino.h>
// #include "can_logic.hpp"
#include "Configurator.hpp"

/***********
 * Defines *
 ***********/

#define LCD_HORZ 480
#define LCD_VERT 320

/***********************
 ***********************
 * NEW STUFF GOES HERE *
 ***********************
 ***********************/

/***********************
 ***********************
 * OLD STUFF GOES HERE *
 ***********************
 ***********************/

/**********
 * Styles *
 **********/

/* Window Styles */
static lv_style_t style_main;   // Main window style
static lv_style_t style_config; // Config window style
static lv_style_t style_flex;   // Flex layout style
/* Radio Button Styles */
static lv_style_t style_radio;     // Style for the radio buttons when not selected
static lv_style_t style_radio_chk; // Style for the radio buttons when selected

/***********
 * Objects *
 ***********/

/* Window Objects */
static lv_obj_t *main_window;   // Main window object
static lv_obj_t *config_window; // Config window object
/* Button Objects */
static lv_obj_t *done_btn; // Done button for config window
/* Label Objects */
static lv_obj_t *done_label; // Done label for config window
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
// LV_FONT_DECLARE(vw_24);
/* Font Objects */
const lv_font_t *top_data_font = &digital_display_100;
const lv_font_t *btm_data_font = &digital_display_140;
const lv_font_t *top_label_font = &vw_16;
const lv_font_t *btm_label_font = &vw_18;
// const lv_font_t *done_label_font = &vw_24;
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
 * Variable Status *
 *******************/

static bool imperial = true;      // Whether the unit radio button selected imperial or not
static uint32_t active_index = 0; // Used to keep track of the active object in the configuration window

/**************************
 **************************
 * END BLOCK OF OLD STUFF *
 **************************
 **************************/

/********************
 * Static Functions *
 ********************/

/***** Initialization *****/

/**
 * Initialize all styles for objects and labels
 */
static void style_init();

/**
 * Initialize/set up the styles for all the Panels.
 */
static void panel_style_init();

/***** Window Creation *****/

/**
 * Open the main screen
 */
static void open_main_window();
/**
 * Initialize the main screen
 *
 * Consists of:
 *
 * 5 buttons (3 small on top row, 2 large on bottom row)
 * Corresponding data labels
 * Corresponding button labels
 */
static void init_main_window();
/**
 * Open the configuration screen
 */
static void open_config_window();
/**
 * Initialize the layout for the configuration screen
 *
 * Consists of:
 *
 * Dropdown menu to select information displayed
 * Radio button to select unit for information
 * Done button
 */
static void init_config_window();

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

/**
 * Update the data in each button
 */
void update_buttons();

/******************
 * Initialization *
 ******************/

static void style_init()
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
    LV_UNUSED(style_btn_top);

    /* Set the style of the bottom data buttons */
    lv_style_init(&style_btn_btm);
    lv_style_set_radius(&style_btn_btm, 2);
    lv_style_set_opa(&style_btn_btm, LV_OPA_100);
    lv_style_set_bg_color(&style_btn_btm, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_text_color(&style_btn_btm, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_text_font(&style_btn_btm, btm_data_font);
    lv_style_set_text_letter_space(&style_btn_btm, 0);
    lv_style_set_shadow_width(&style_btn_btm, 0);
    LV_UNUSED(style_btn_btm);

    /* Set the style of the "done" button */
    lv_style_init(&style_btn_done);
    lv_style_set_radius(&style_btn_done, 5);
    lv_style_set_opa(&style_btn_done, LV_OPA_100);
    lv_style_set_bg_color(&style_btn_done, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_text_color(&style_btn_done, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_text_font(&style_btn_done, drop_down_font);
    lv_style_set_text_letter_space(&style_btn_done, 0);
    lv_style_set_shadow_width(&style_btn_done, 0);

    /* Darken button when pressed */
    static lv_color_filter_dsc_t color_filter_btn;
    lv_color_filter_dsc_init(&color_filter_btn, darken);
    lv_style_init(&style_btn_pressed);
    lv_style_set_color_filter_dsc(&style_btn_pressed, &color_filter_btn);
    lv_style_set_color_filter_opa(&style_btn_pressed, LV_OPA_20);
    LV_UNUSED(style_btn_pressed);

    /* Set the style of the top row label object */
    lv_style_init(&style_label_top);
    lv_style_set_text_color(&style_label_top, lv_palette_lighten(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(&style_label_top, top_label_font);
    lv_style_set_text_letter_space(&style_label_top, 0);
    LV_UNUSED(style_label_top);

    /* Set the style of the bottom row label object */
    lv_style_init(&style_label_btm);
    lv_style_set_text_color(&style_label_btm, lv_palette_lighten(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(&style_label_btm, btm_label_font);
    lv_style_set_text_letter_space(&style_label_btm, 0);
    LV_UNUSED(style_label_btm);

    /* Set the style of the label in the done button */
    lv_style_init(&style_label_done);
    lv_style_set_text_color(&style_label_done, lv_palette_darken(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(&style_label_done, drop_down_font);
    lv_style_set_text_letter_space(&style_label_done, 0);

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

    
    panel_style_init();
}

static void panel_style_init()
{
    /***** Top Left Panel *****/
    panel_tl.style_btn = &style_btn_top;
    panel_tl.style_btn_pressed = &style_btn_pressed;
    panel_tl.style_label = &style_label_top;
    /***** Top Center Panel *****/
    panel_tc.style_btn = &style_btn_top;
    panel_tc.style_btn_pressed = &style_btn_pressed;
    panel_tc.style_label = &style_label_top;
    /***** Top Right Panel *****/
    panel_tr.style_btn = &style_btn_top;
    panel_tr.style_btn_pressed = &style_btn_pressed;
    panel_tr.style_label = &style_label_top;
    /***** Bottom Left Panel *****/
    panel_bl.style_btn = &style_btn_btm;
    panel_bl.style_btn_pressed = &style_btn_pressed;
    panel_bl.style_label = &style_label_btm;
    /***** Bottom Right Panel *****/
    panel_br.style_btn = &style_btn_btm;
    panel_br.style_btn_pressed = &style_btn_pressed;
    panel_br.style_label = &style_label_btm;
}

/*******************
 * Window Creation *
 *******************/

static void open_main_window()
{
    lv_screen_load(main_window);
}

static void init_main_window()
{
    main_window = lv_obj_create(NULL);
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

static void open_config_window()
{
    lv_screen_load(config_window);
}

static void init_config_window(void)
{
    config_window = lv_obj_create(NULL);
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

static void add_main_objects()
{
    panel_tl.btn = lv_btn_create(main_window);
    lv_obj_add_style(panel_tl.btn, panel_tl.style_btn, 0);
    lv_obj_add_style(panel_tl.btn, panel_tl.style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(panel_tl.btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_add_event_cb(panel_tl.btn, button_config_cb, LV_EVENT_CLICKED, panel_tl.btn);
    panel_tl.data = lv_label_create(panel_tl.btn);
    lv_label_set_text(panel_tl.data, panel_tl.panel_info->get_data_str().c_str());
    lv_obj_align(panel_tl.data, LV_ALIGN_CENTER, 0, 0);

    panel_tc.btn = lv_btn_create(main_window);
    lv_obj_add_style(panel_tc.btn, panel_tc.style_btn, 0);
    lv_obj_add_style(panel_tc.btn, panel_tc.style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(panel_tc.btn, LV_GRID_ALIGN_STRETCH, 2, 2, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_add_event_cb(panel_tc.btn, button_config_cb, LV_EVENT_CLICKED, panel_tc.btn);
    panel_tc.data = lv_label_create(panel_tc.btn);
    lv_label_set_text(panel_tc.data, panel_tc.panel_info->get_data_str().c_str());
    lv_obj_align(panel_tc.data, LV_ALIGN_CENTER, 0, 0);

    panel_tr.btn = lv_btn_create(main_window);
    lv_obj_add_style(panel_tr.btn, panel_tr.style_btn, 0);
    lv_obj_add_style(panel_tr.btn, panel_tr.style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(panel_tr.btn, LV_GRID_ALIGN_STRETCH, 4, 2, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_add_event_cb(panel_tr.btn, button_config_cb, LV_EVENT_CLICKED, panel_tr.btn);
    panel_tr.data = lv_label_create(panel_tr.btn);
    lv_label_set_text(panel_tr.data, panel_tr.panel_info->get_data_str().c_str());
    lv_obj_align(panel_tr.data, LV_ALIGN_CENTER, 0, 0);

    panel_bl.btn = lv_btn_create(main_window);
    lv_obj_add_style(panel_bl.btn, panel_bl.style_btn, 0);
    lv_obj_add_style(panel_bl.btn, panel_bl.style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(panel_bl.btn, LV_GRID_ALIGN_STRETCH, 0, 3, LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_add_event_cb(panel_bl.btn, button_config_cb, LV_EVENT_CLICKED, panel_bl.btn);
    panel_bl.data = lv_label_create(panel_bl.btn);
    lv_label_set_text(panel_bl.data, panel_bl.panel_info->get_data_str().c_str());
    lv_obj_align(panel_bl.data, LV_ALIGN_CENTER, 0, 0);

    panel_br.btn = lv_btn_create(main_window);
    lv_obj_add_style(panel_br.btn, panel_br.style_btn, 0);
    lv_obj_add_style(panel_br.btn, panel_br.style_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_grid_cell(panel_br.btn, LV_GRID_ALIGN_STRETCH, 3, 3, LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_add_event_cb(panel_br.btn, button_config_cb, LV_EVENT_CLICKED, panel_br.btn);
    panel_br.data = lv_label_create(panel_br.btn);
    lv_label_set_text(panel_br.data, panel_br.panel_info->get_data_str().c_str());
    lv_obj_align(panel_br.data, LV_ALIGN_CENTER, 0, 0);
}

static void add_main_labels()
{
    panel_tl.label = lv_label_create(main_window);
    lv_label_set_text(panel_tl.label, panel_tl.panel_info->get_name_and_label().c_str());
    lv_obj_add_style(panel_tl.label, panel_tl.style_label, 0);
    lv_obj_set_grid_cell(panel_tl.label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1);

    panel_tc.label = lv_label_create(main_window);
    lv_label_set_text(panel_tc.label, panel_tc.panel_info->get_name_and_label().c_str());
    lv_obj_add_style(panel_tc.label, panel_tc.style_label, 0);
    lv_obj_set_grid_cell(panel_tc.label, LV_GRID_ALIGN_CENTER, 2, 2, LV_GRID_ALIGN_CENTER, 1, 1);

    panel_tr.label = lv_label_create(main_window);
    lv_label_set_text(panel_tr.label, panel_tr.panel_info->get_name_and_label().c_str());
    lv_obj_add_style(panel_tr.label, panel_tr.style_label, 0);
    lv_obj_set_grid_cell(panel_tr.label, LV_GRID_ALIGN_CENTER, 4, 2, LV_GRID_ALIGN_CENTER, 1, 1);

    panel_bl.label = lv_label_create(main_window);
    lv_label_set_text(panel_bl.label, panel_bl.panel_info->get_name_and_label().c_str());
    lv_obj_add_style(panel_bl.label, panel_bl.style_label, 0);
    lv_obj_set_grid_cell(panel_bl.label, LV_GRID_ALIGN_CENTER, 0, 3, LV_GRID_ALIGN_CENTER, 3, 1);

    panel_br.label = lv_label_create(main_window);
    lv_label_set_text(panel_br.label, panel_br.panel_info->get_name_and_label().c_str());
    lv_obj_add_style(panel_br.label, panel_br.style_label, 0);
    lv_obj_set_grid_cell(panel_br.label, LV_GRID_ALIGN_CENTER, 3, 3, LV_GRID_ALIGN_CENTER, 3, 1);
}

static void add_config_objects()
{
    /* Create layout */
    flex_col_1 = lv_obj_create(config_window);
    lv_obj_set_flex_flow(flex_col_1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(flex_col_1, lv_pct(65), lv_pct(100));
    lv_obj_add_style(flex_col_1, &style_flex, 0);
    flex_col_2 = lv_obj_create(config_window);
    lv_obj_set_flex_flow(flex_col_2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(flex_col_2, lv_pct(35), lv_pct(100));
    lv_obj_add_event_cb(flex_col_2, radio_cb, LV_EVENT_CLICKED, &active_index);
    lv_obj_set_x(flex_col_2, lv_pct(65));
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
    lv_obj_add_style(done_btn, &style_btn_done, 0);
    lv_obj_set_size(done_btn, 100, 55);
    lv_obj_set_pos(done_btn, 360, 245); // 20px buffer on top and side (x: 480 - btn width - 20, y: 320 - btn height - 20)
    /* Turn off scrolling */
    lv_obj_set_scrollbar_mode(done_btn, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(done_btn, LV_OBJ_FLAG_SCROLLABLE);
    /* Add "Done" text */
    done_label = lv_label_create(done_btn);
    lv_obj_add_style(done_label, &style_label_done, 0);
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
        open_config_window();
        // top_left_panel.set_data(curr_select_object);
    }
}

static void close_window_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
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