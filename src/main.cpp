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

bool test = false;
int counter = 0;

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

  /* This isn't working rn */

  panel_tl.panel_info->set_is_metric(false);
  panel_tc.panel_info->set_is_metric(false);
  panel_tr.panel_info->set_is_metric(false);
  panel_bl.panel_info->set_is_metric(false);
  panel_br.panel_info->set_is_metric(false);
}

void setup()
{
  lv_init();

  set_unit_region();

  lv_task_handler();

  style_init();

  lcd_init();
  display_init();

  // init_main_window();
  // init_config_window();

  // open_config_window();
  // open_main_window();

  main_layout.init_layout();

  main_layout.open_window();

  lv_tick_set_cb(millis);
}

void loop()
{
  // if (test)
  // {
  //   open_main_window();
  // }
  // else
  // {
  //   main_layout.open_window(); 
  // }

  lv_task_handler();

  test = !test;

  if (!(counter % 3))
  {
    if (test)
    {
      panel_tl.panel_info = &ambient_pressure;
    }
    else
    {
      panel_tl.panel_info = &coolant_temp;
    }
    lv_label_set_text(panel_tl.data, panel_tl.panel_info->get_data_str().c_str());
    lv_label_set_text(panel_tl.label, panel_tl.panel_info->get_name_and_label().c_str());
  }

  counter++;

  delay(1000);
}