#include "can_handler.hpp"

/***********
 * Defines *
 ***********/

#define LCD_HORZ 480
#define LCD_VERT 320

/*********
 * Fonts *
 *********/

/***********
 * Display *
 ***********/

static lv_display_t *display;
static lv_color_t buf_1[(LCD_HORZ * LCD_VERT) / 10];
static lv_color_t buf_2[(LCD_HORZ * LCD_VERT) / 10];

/***************
 * Touchscreen *
 ***************/

static lv_indev_t *touch;

/********************
 * Static Functions *
 ********************/

/***** Initialization *****/

/**
 * Initialize all styles for objects and labels
 */
static void style_init();

/***** Transforms *****/

/**
 * Darken the button by the specified opacity
 */
static lv_color_t darken(const lv_color_filter_dsc_t *dsc, lv_color_t color, lv_opa_t opa);

/******************
 * Initialization *
 ******************/

static void style_init()
{
    /* Darken button when pressed */
    static lv_color_filter_dsc_t color_filter_btn;
    lv_color_filter_dsc_init(&color_filter_btn, darken);
    lv_style_init(&style_btn_pressed);
    lv_style_set_color_filter_dsc(&style_btn_pressed, &color_filter_btn);
    lv_style_set_color_filter_opa(&style_btn_pressed, LV_OPA_20);
    LV_UNUSED(style_btn_pressed);
}

/**************
 * Transforms *
 **************/

static lv_color_t darken(const lv_color_filter_dsc_t *dsc, lv_color_t color, lv_opa_t opa)
{
    LV_UNUSED(dsc);
    return lv_color_darken(color, opa);
}

/**********
 * Timers *
 **********/

void panel_tl_timer_cb(lv_timer_t *timer)
{
    can_send(panel_tl.panel_info); 
}

void panel_tc_timer_cb(lv_timer_t *timer)
{
    can_send(panel_tc.panel_info);
}

void panel_tr_timer_cb(lv_timer_t *timer)
{
    can_send(panel_tr.panel_info);
}

void panel_bl_timer_cb(lv_timer_t *timer)
{
    can_send(panel_bl.panel_info);
}

void panel_br_timer_cb(lv_timer_t *timer)
{
    can_send(panel_br.panel_info);
}