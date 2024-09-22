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

void lcd_init()
{
  lcd.begin(12);
  lcd.setBitDepth(16);
  lcd.setRotation(1);
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

void set_unit_region()
{
  // Pull from EEPROM

  top_left_panel.set_is_metric(false);
  top_center_panel.set_is_metric(false);
  top_right_panel.set_is_metric(false);
  bottom_left_panel.set_is_metric(false);
  bottom_right_panel.set_is_metric(false);
}

void setup()
{
  lv_init();

  set_unit_region();

  lcd_init();
  display_init();

  lv_task_handler();

  delay(1000);

  style_init();

  init_main_window();
  init_config_window();

  open_main_window();
  // open_config_window();

  lv_tick_set_cb(millis);
}

void loop()
{
  lv_task_handler();

  delay(10);
}