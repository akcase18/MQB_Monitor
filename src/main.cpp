#include <queue>
#include "can_handler.hpp"
#include "ILI948x_t4_mm.h"
#include "EEPROM.h"
#include "touchscreen.hpp"

#define USE_TOUCH 1
#define NIGHT 1

#if NIGHT
#define SCRN_BRIGHTNESS 30
#else
#define SCRN_BRIGHTNESS 100
#endif

/***********
 * Defines *
 ***********/

#define PWM_PIN 3
#define BRIGHTNESS 1
#define MAX_BRIGHTNESS 256

/***********************
 * Display Constructor *
 ***********************/

static lv_display_t *display;
static lv_color_t buf_1[(LCD_HORZ * LCD_VERT) / 10];
static lv_color_t buf_2[(LCD_HORZ * LCD_VERT) / 10];

/* D0 */
#define LCD_CS 4
/* D1 */
#define LCD_DC 5
/* G9 */
#define LCD_RST 32
ILI948x_t4_mm lcd = ILI948x_t4_mm(LCD_DC, LCD_CS, LCD_RST);

/*********************
 * Touch Constructor *
 *********************/

static lv_indev_t *touch;

/* CS1 */
#define CS_PIN 0
/* G10 */
#define TIRQ_PIN 33
Touchscreen ts = Touchscreen(CS_PIN);

/*********
 * Queue *
 *********/

static queue<Information *> can_queue; // This queue holds the CAN messages that are ready to be requested (sent)

/**************************************
 * Initialization and Darken Function *
 **************************************/

static lv_color_t darken(const lv_color_filter_dsc_t *dsc, lv_color_t color, lv_opa_t opa)
{
  LV_UNUSED(dsc);
  return lv_color_darken(color, opa);
}

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

bool test = false;
int counter = 0;

/*************
 * Callbacks *
 *************/

void panel_select_cb(lv_event_t *e)
{
  if (lv_event_get_code(e) == LV_EVENT_CLICKED)
  {
    config.open_window();
  }
}

void config_done_cb(lv_event_t *e)
{
  if (lv_event_get_code(e) == LV_EVENT_CLICKED)
  {
    layout.open_window();
  }
}

void set_cbs()
{
  lv_obj_add_event_cb(layout.panel_tl->btn, panel_select_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(layout.panel_tc->btn, panel_select_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(layout.panel_tr->btn, panel_select_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(layout.panel_bl->btn, panel_select_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(layout.panel_br->btn, panel_select_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_add_event_cb(config.btn_done, config_done_cb, LV_EVENT_CLICKED, NULL);
}

/*********
 * Setup *
 *********/

/**
 * LCD Wiring
 *
 * Pin 10: CS --> WR: 24
 * Pin 40: G0 --> D0: 3
 * Pin 41: G1 --> D1: 4
 * Pin 42: G2 --> D2: 5
 * Pin 43: G3 --> D3: 6
 * Pin 44: G4 --> D4: 7
 * Pin 45: G5 --> D5: 8
 * Pin 6:  G6 --> D6: 9
 * Pin 9:  G7 --> D7: 10
 * Pin 12: CIPO --> RD: 26
 */

/**
 * Sets the brightness of the display from 0-100% by setting
 * the duty cycle of a PWM pin connected to the backlight
 * control pin on the display
 *
 * @param percent brightness of the display (0-100%)
 */
void set_brightness(uint8_t percent)
{
  /* Clamp brightness if over 100% */
  if (percent > 100)
  {
    percent = 100;
  }
  pinMode(PWM_PIN, OUTPUT);                  // Set up pin to control brightness
  int brightness = (percent * MAX_BRIGHTNESS) / 100; // Multiply the percent brightness level by the max brighness value
  analogWrite(PWM_PIN, brightness);
}

/**
 * Initialize the display driver object
 */
void lcd_init()
{
  lcd.begin(12);
  lcd.setBitDepth(16);
  lcd.setRotation(1);
  // lcd.onCompleteCB(dma_complete);
}

/**
 * Set up the function to push pixels to the display when
 * refreshing it
 */
void display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *data)
{
  lcd.pushPixels16bitDMA((uint16_t *)data, area->x1, area->y1, area->x2, area->y2);
  lv_display_flush_ready(disp);
}

/**
 * Set up the display, including the display driver and
 * registering the display for LVGL
 */
void display_init()
{
  set_brightness(SCRN_BRIGHTNESS); // Can pull from EEPROM or something
  lcd_init();
  display = lv_display_create(LCD_HORZ, LCD_VERT);
  lv_display_set_flush_cb(display, display_flush);
  // lv_display_set_flush_wait_cb(display, display_flush_wait_cb);
  lv_display_set_buffers(display, buf_1, buf_2, sizeof(buf_1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
  if (ts.touched())
  {
    data->point.x = (ts.get_x() * LCD_HORZ) / 4095;
    data->point.y = (ts.get_y() * LCD_VERT) / 4095;
    data->state = LV_INDEV_STATE_PRESSED;
  }
  else
  {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void touch_init()
{
  ts.start();
  touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, touch_read);
  // lv_indev_set_mode(touch, LV_INDEV_MODE_EVENT);
}

/**
 * This will eventually pull this data from EEPROM so that
 * configs can be saved, but this allows you to choose whether
 * to use imperial or metric units for the individual panels.
 */
void set_unit_region()
{
  // Pull from EEPROM

  panel_tl.panel_info->set_is_metric(false);
  panel_tc.panel_info->set_is_metric(false);
  panel_tr.panel_info->set_is_metric(false);
  panel_bl.panel_info->set_is_metric(false);
  panel_br.panel_info->set_is_metric(false);

  // panel_tl.panel_info.set_is_metric(true);
  // panel_tc.panel_info.set_is_metric(true);
  // panel_tr.panel_info.set_is_metric(true);
  // panel_bl.panel_info.set_is_metric(true);
  // panel_br.panel_info.set_is_metric(true);
}

/**
 * Initializes the main and configurator window.
 */
void init_windows()
{
  layout.init_layout();       // Initialize the main window
  config.init_configurator(); // Initialize the configurator window
}

/**
 * Initializes all of the timers. These timers add CAN messages to the queue to be
 * sent off to the car.
 */
void init_timers()
{
  lv_timer_t *panel_tl_timer = lv_timer_create(panel_tl_timer_cb, panel_tl.panel_info->get_refresh_rate(), NULL);
  lv_timer_t *panel_tc_timer = lv_timer_create(panel_tc_timer_cb, panel_tc.panel_info->get_refresh_rate(), NULL);
  lv_timer_t *panel_tr_timer = lv_timer_create(panel_tr_timer_cb, panel_tr.panel_info->get_refresh_rate(), NULL);
  lv_timer_t *panel_bl_timer = lv_timer_create(panel_bl_timer_cb, panel_bl.panel_info->get_refresh_rate(), NULL);
  lv_timer_t *panel_br_timer = lv_timer_create(panel_br_timer_cb, panel_br.panel_info->get_refresh_rate(), NULL);

  lv_timer_t *can_send_timer = lv_timer_create(can_send_timer_cb, 10, NULL);
}

/**
 * Updates the data within all Panels based on the data in the Information objects.
 */
void update_display()
{
  lv_label_set_text(panel_tl.data, panel_tl.panel_info->get_data_str().c_str());
  lv_label_set_text(panel_tc.data, panel_tc.panel_info->get_data_str().c_str());
  lv_label_set_text(panel_tr.data, panel_tr.panel_info->get_data_str().c_str());
  lv_label_set_text(panel_bl.data, panel_bl.panel_info->get_data_str().c_str());
  lv_label_set_text(panel_br.data, panel_br.panel_info->get_data_str().c_str());
}

void setup()
{
  Serial.begin(115200);

  lv_init();

  set_unit_region();

  style_init();

  can_init();

  display_init();

  #if USE_TOUCH
  delay(100);
  touch_init();
  #endif

  init_windows();

  layout.open_window();

  init_timers();
  lv_tick_set_cb(millis);

  set_cbs();
}

unsigned long current_time = 0;

void loop()
{
  if (millis() > current_time + 500)
  {
    if (test)
    {
      
    }
    else
    {
      
    }
    test = !test;
    current_time = millis();
  }

  counter++;

  // #if USE_TOUCH
  // lv_indev_read(touch);
  // #endif

  lv_task_handler();

  update_display();

  can_loop();
}