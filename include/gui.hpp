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

/*********
 * Queue *
 *********/

static queue<Information *> can_queue; // This queue holds the CAN messages that are ready to be requested (sent)

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
 * Darken the button by the specified opacity.
 * 
 * @note for use when a button is pressed
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

/**
 * Adds the Information object held in the top left panel
 * to the CAN queue to be sent off.
 */
void panel_tl_timer_cb(lv_timer_t *timer)
{
    can_queue.push(panel_tl.panel_info);
}

/**
 * Adds the Information object held in the top center panel
 * to the CAN queue to be sent off.
 */
void panel_tc_timer_cb(lv_timer_t *timer)
{
    can_queue.push(panel_tc.panel_info);
}

/**
 * Adds the Information object held in the top right panel
 * to the CAN queue to be sent off.
 */
void panel_tr_timer_cb(lv_timer_t *timer)
{
    can_queue.push(panel_tr.panel_info);
}

/**
 * Adds the Information object held in the bottom left panel
 * to the CAN queue to be sent off.
 */
void panel_bl_timer_cb(lv_timer_t *timer)
{
    can_queue.push(panel_bl.panel_info);
}

/**
 * Adds the Information object held in the bottom right panel
 * to the CAN queue to be sent off.
 */
void panel_br_timer_cb(lv_timer_t *timer)
{
    can_queue.push(panel_br.panel_info);
}

/**
 * Calls can_send() to send the first CAN message in the queue.
 */
void can_send_timer_cb(lv_timer_t *timer)
{
    if (!can_queue.empty())
    {
        can_send(can_queue.front());
        can_queue.pop();
    }
}