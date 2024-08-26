#include "can_handler.hpp"

/********************
 * Global Constants *
 ********************/

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

/* Button Styles */
static lv_style_t style_btn_top;     // Top row button style
static lv_style_t style_btn_btm;     // Bottom row button style
static lv_style_t style_btn_pressed; // Button style when pressed 
/* Label Styles */
static lv_style_t style_label_top; // Top row label style
static lv_style_t style_label_btm; // Bottom row label style

/******************
 * Display Buffer *
 ******************/

static lv_color_t disp_buf_1[(LCD_HORZ * LCD_VERT) / 10];
static lv_color_t disp_buf_2[(LCD_HORZ * LCD_VERT) / 10];

/********************
 * Static Functions *
 ********************/

/***** Initialization *****/

/**
 * Initialize all styles for objects and labels
 */
static void style_init(void);

/***** Transforms *****/

/**
 * Darken the button by the specified opacity
 */
static lv_color_t darken(const lv_color_filter_dsc_t *dsc, lv_color_t color, lv_opa_t opa);

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

    /* Set the style of the top data buttons */
    lv_style_init(&style_btn_top);
    lv_style_set_radius(&style_btn_top, 2);
    lv_style_set_opa(&style_btn_top, LV_OPA_100);
    lv_style_set_bg_color(&style_btn_top, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_text_color(&style_btn_top, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_text_font(&style_btn_top, &digital_display_100);
    lv_style_set_text_letter_space(&style_btn_top, 0);
    lv_style_set_shadow_width(&style_btn_top, 0);

    /* Set the style of the bottom data buttons */
    lv_style_init(&style_btn_btm);
    lv_style_set_radius(&style_btn_btm, 2);
    lv_style_set_opa(&style_btn_btm, LV_OPA_100);
    lv_style_set_bg_color(&style_btn_btm, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_text_color(&style_btn_btm, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_text_font(&style_btn_btm, &digital_display_140);
    lv_style_set_text_letter_space(&style_btn_btm, 0);
    lv_style_set_shadow_width(&style_btn_btm, 0);

    /* Darken button when pressed */
    static lv_color_filter_dsc_t color_filter_btn;
    lv_color_filter_dsc_init(&color_filter_btn, darken);
    lv_style_init(&style_btn_pressed);
    lv_style_set_color_filter_dsc(&style_btn_pressed, &color_filter_btn);
    lv_style_set_color_filter_opa(&style_btn_pressed, LV_OPA_20);

    /* Set the style of the top row label object */
    lv_style_init(&style_label_top);
    lv_style_set_text_color(&style_label_top, lv_palette_lighten(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(&style_label_top, &vw_16);
    lv_style_set_text_letter_space(&style_label_top, 0);

    /* Set the style of the bottom row label object */
    lv_style_init(&style_label_btm);
    lv_style_set_text_color(&style_label_btm, lv_palette_lighten(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(&style_label_btm, &vw_18);
    lv_style_set_text_letter_space(&style_label_btm, 0);
}

/**************
 * Transforms *
 **************/

static lv_color_t darken(const lv_color_filter_dsc_t *dsc, lv_color_t color, lv_opa_t opa)
{
    LV_UNUSED(dsc);
    return lv_color_darken(color, opa);
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