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

/*********************
 * Touch Constructor *
 *********************/

#define CS_PIN 8
XPT2046_Touchscreen ts = XPT2046_Touchscreen(CS_PIN);

bool test = false;
int counter = 0;

FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> Can0;

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
  int brightness = percent * MAX_BRIGHTNESS; // Multiply the percent brightness level by the max brighness value
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
  set_brightness(100); // Can pull from EEPROM or something
  lcd_init();
  display = lv_display_create(LCD_HORZ, LCD_VERT);
  lv_display_set_flush_cb(display, display_flush);
  lv_display_set_buffers(display, buf_1, buf_2, sizeof(buf_1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
  if (ts.touched())
  {
    TS_Point ts_point = ts.getPoint();
    data->point.x = ts_point.x;
    data->point.y = ts_point.y;
    data->state = LV_INDEV_STATE_PRESSED;
  }
  else
  {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void touch_init()
{
  ts.begin();
  ts.setRotation(1);
  touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, touch_read);
}

void set_unit_region()
{
  // Pull from EEPROM

  panel_tl.panel_info->set_is_metric(false);
  panel_tc.panel_info->set_is_metric(false);
  panel_tr.panel_info->set_is_metric(false);
  panel_bl.panel_info->set_is_metric(false);
  panel_br.panel_info->set_is_metric(false);
}

void init_windows()
{
  layout.init_layout();       // Initialize the main window
  config.init_configurator(); // Initialize the configurator window
}

void canSniff(const CAN_message_t &msg)
{
  Serial.print("MB ");
  Serial.print(msg.mb);
  Serial.print("  OVERRUN: ");
  Serial.print(msg.flags.overrun);
  Serial.print("  LEN: ");
  Serial.print(msg.len);
  Serial.print(" EXT: ");
  Serial.print(msg.flags.extended);
  Serial.print(" TS: ");
  Serial.print(msg.timestamp);
  Serial.print(" ID: ");
  Serial.print(msg.id, HEX);
  Serial.print(" Buffer: ");
  for (uint8_t i = 0; i < msg.len; i++)
  {
    Serial.print(msg.buf[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

void setup()
{
  lv_init();

  set_unit_region();

  lv_task_handler();

  style_init();

  can_init();

  display_init();
  // touch_init();

  init_windows();

  init_windows();

  layout.open_window();

  config.open_window();

  config.open_warning_config();

  lv_tick_set_cb(millis);
}

void loop()
{
  if (!(counter % 6))
  {
    if (test)
    {
      // layout.set_warning(&panel_tl, true);
      // layout.set_warning(&panel_tc, true);
      // layout.set_warning(&panel_tr, true);
      // layout.set_warning(&panel_bl, true);
      // layout.set_warning(&panel_br, true);
    }
    else
    {
      // layout.set_warning(&panel_tl, false);
      // layout.set_warning(&panel_tc, false);
      // layout.set_warning(&panel_tr, false);
      // layout.set_warning(&panel_bl, false);
      // layout.set_warning(&panel_br, false);
    }
    test = !test;
  }

  counter++;

  lv_task_handler();

  can_loop();

  lv_delay_ms(500);
}