#include "gui.hpp"
#include "ILI948x_t4_mm.h"
#include "XPT2046_Touchscreen.h"
#include "SPI.h"

/***********
 * Defines *
 ***********/

#define PWM_PIN 3
#define BRIGHTNESS 1
#define MAX_BRIGHTNESS 256

/***********************
 * Display Constructor *
 ***********************/

#define LCD_CS 4   // D0
#define LCD_DC 5   // D1
#define LCD_RST 26 // G8
ILI948x_t4_mm lcd = ILI948x_t4_mm(LCD_DC, LCD_CS, LCD_RST);

/* LCD Wiring */
// Pin 10: CS --> WR: 24
// Pin 40: G0 --> D0: 3
// Pin 41: G1 --> D1: 4
// Pin 42: G2 --> D2: 5
// Pin 43: G3 --> D3: 6
// Pin 44: G4 --> D4: 7
// Pin 45: G5 --> D5: 8
// Pin 6:  G6 --> D6: 9
// Pin 9:  G7 --> D7: 10
// Pin 12: CIPO --> RD: 26

/*********************
 * Touch Constructor *
 *********************/

#define CS_PIN 36 // CS1
#define TIRQ_PIN 2
XPT2046_Touchscreen ts(CS_PIN, TIRQ_PIN);

/* Touch Controller Wiring */
// Pin 36: CS1 --> RTP_/CS: 30
// Pin 34: CIPO2 --> SDO: 32
// Pin 35: COPI2 --> SDI: 34
// Pin 37: SCK2 --> SCL: 33
// Pin 4: D0 --> /RTP PEN: 31

lv_timer_t *top_left_timer;
lv_timer_t *top_center_timer;
lv_timer_t *top_right_timer;
lv_timer_t *bottom_left_timer;
lv_timer_t *bottom_right_timer;

lv_timer_t *can_test_timer;

uint16_t x_val = 0;

void set_brightness()
{
  pinMode(PWM_PIN, OUTPUT);
  int brightness = BRIGHTNESS * MAX_BRIGHTNESS;
  analogWrite(PWM_PIN, brightness);
}

void set_units()
{
  top_left_panel.set_unit_region(Information::Unit_Region::IMPERIAL);
  top_center_panel.set_unit_region(Information::Unit_Region::IMPERIAL);
  top_right_panel.set_unit_region(Information::Unit_Region::IMPERIAL);
  bottom_left_panel.set_unit_region(Information::Unit_Region::IMPERIAL);
  bottom_right_panel.set_unit_region(Information::Unit_Region::IMPERIAL);
}

void lcd_init()
{
  lcd.begin(12);
  lcd.setBitDepth(16);
  lcd.setRotation(1);
}

void ts_init()
{
  ts.begin(SPI);
  ts.setRotation(1);
}

void display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *data)
{
  lcd.pushPixels16bitDMA((uint16_t *)data, area->x1, area->y1, area->x2, area->y2);
  lv_display_flush_ready(disp);
}

void display_init()
{
  display = lv_display_create(LCD_HORZ, LCD_VERT);
  lv_display_set_flush_cb(display, display_flush);
  lv_display_set_buffers(display, buf_1, buf_2, sizeof(buf_1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

uint8_t test = 0;

// void touch_read(lv_indev_t *touch, lv_indev_data_t *data)
// {
//   if (ts.tirqTouched())
//   {
//     TS_Point ts_point = ts.getPoint();
//     data->point.x = ts_point.x;
//     data->point.y = ts_point.y;
//     // data->point.x = 380;
//     // data->point.y = 280;
//     data->state = LV_INDEV_STATE_PRESSED;

//     top_left_panel.set_data(data->point.x);
//     top_center_panel.set_data(data->point.y);
//     bottom_left_panel.set_data(test++);
//     bottom_right_panel.set_data(ts_point.z);

//     ts.isrWake = false;
//   }
//   else
//   {
//     data->state = LV_INDEV_STATE_RELEASED;

//     TS_Point ts_point = ts.getPoint();
//     data->point.x = ts_point.x;
//     data->point.y = ts_point.y;

//     top_left_panel.set_data(data->point.x);
//     top_center_panel.set_data(data->point.y);
//     bottom_left_panel.set_data(test++);
//     bottom_right_panel.set_data(ts_point.z);
//   }
// }

// void touch_init()
// {
//   touch_indev = lv_indev_create();
//   lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
//   lv_indev_set_read_cb(touch_indev, touch_read);
// }

void timer_init()
{
  top_left_timer = lv_timer_create_basic();
  top_center_timer = lv_timer_create_basic();
  top_right_timer = lv_timer_create_basic();
  bottom_left_timer = lv_timer_create_basic();
  bottom_right_timer = lv_timer_create_basic();

  lv_timer_set_cb(top_left_timer, top_left_timer_cb);
  lv_timer_set_period(top_left_timer, top_left_panel.get_refresh_rate());

  lv_timer_set_cb(top_center_timer, top_center_timer_cb);
  lv_timer_set_period(top_center_timer, top_center_panel.get_refresh_rate());

  lv_timer_set_cb(top_right_timer, top_right_timer_cb);
  lv_timer_set_period(top_right_timer, top_right_panel.get_refresh_rate());

  lv_timer_set_cb(bottom_left_timer, bottom_left_timer_cb);
  lv_timer_set_period(bottom_left_timer, bottom_left_panel.get_refresh_rate());

  lv_timer_set_cb(bottom_right_timer, bottom_right_timer_cb);
  lv_timer_set_period(bottom_right_timer, bottom_right_panel.get_refresh_rate());
}

void setup()
{
  set_brightness();
  set_units();

  lv_init();

  lcd_init();
  display_init();

  lv_task_handler();

  delay(1000);

  // ts_init();
  // touch_init();

  can_init();

  style_init();

  main_window_create();

  lv_tick_set_cb(millis);

  timer_init();
}

void loop()
{
  lv_task_handler();
  // can_loop();

  // send_msg( top_left_panel);

  delay(10);

  // send_msg(top_left_panel);
  // lv_label_set_text(lv_obj_get_child(top_left, 0), top_left_panel.get_data_str());
  // send_msg(top_center_panel);
  // lv_label_set_text(lv_obj_get_child(top_center, 0), top_center_panel.get_data_str());
  // send_msg(top_right_panel);
  // lv_label_set_text(lv_obj_get_child(top_right, 0), top_right_panel.get_data_str());
  // send_msg(bottom_left_panel);
  // lv_label_set_text(lv_obj_get_child(bottom_left, 0), bottom_left_panel.get_data_str());
  // send_msg(bottom_right_panel);
  // lv_label_set_text(lv_obj_get_child(bottom_right, 0), bottom_right_panel.get_data_str());
}