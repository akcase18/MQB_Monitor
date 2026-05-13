#ifdef USE_ARDUINO
    #include "ILI948x_t4_mm.h"
    #include "can_handler.hpp"
    #include "touchscreen.hpp"
#else
    #include "app_hal.h"
#endif

#include "setup.hpp"
#include "Screen.hpp"

/*==============================================================================
 Display Constructor
 =============================================================================*/

static lv_display_t *display;
static lv_color_t    buf_1[(LCD_HORZ * LCD_VERT) / 10];
static lv_color_t    buf_2[(LCD_HORZ * LCD_VERT) / 10];

/* D0 */
#define LCD_CS 4
/* D1 */
#define LCD_DC 5
/* G9 */
#define LCD_RST 32
#ifdef USE_ARDUINO
ILI948x_t4_mm lcd = ILI948x_t4_mm(LCD_DC, LCD_CS, LCD_RST);
#endif

/*==============================================================================
 Touch Constructor
 =============================================================================*/

static lv_indev_t *touch;

/* CS1 */
#define CS_PIN 0
/* G10 */
#define TIRQ_PIN 33
#ifdef USE_ARDUINO
Touchscreen ts = Touchscreen(CS_PIN);
#endif

/*==============================================================================
 Queue
 =============================================================================*/

/*==============================================================================
 Timers
 =============================================================================*/

lv_timer_t *can_send_timer;

bool test    = false;
int  counter = 0;

/*==============================================================================
 Callbacks
 =============================================================================*/

/*==============================================================================
 Setup
 =============================================================================*/

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
 * Initialize the display driver object
 */
void lcd_init()
{
#ifdef USE_ARDUINO
    lcd.begin(12);
    lcd.setBitDepth(16);
    lcd.setRotation(1);
#endif
}

/**
 * Set up the function to push pixels to the display when
 * refreshing it
 */
void display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *data)
{
#ifdef USE_ARDUINO
    lcd.pushPixels16bitDMA(
        (uint16_t *)data, area->x1, area->y1, area->x2, area->y2);
    lv_display_flush_ready(disp);
#endif
}

/**
 * Set up the display, including the display driver and
 * registering the display for LVGL
 */
void display_init()
{
    lcd_init();
    display = lv_display_create(LCD_HORZ, LCD_VERT);
    lv_display_set_flush_cb(display, display_flush);
    lv_display_set_buffers(
        display, buf_1, buf_2, sizeof(buf_1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

/**
 * Callback function called by the touchscreen handler. If
 * the display is touched, it will register a touch with LVGL.
 */
void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
#ifdef USE_ARDUINO
    if (ts.touched())
    {
        data->point.x = (ts.get_x() * LCD_HORZ) / 4095;
        data->point.y = (ts.get_y() * LCD_VERT) / 4095;
        data->state   = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
#endif
}

/**
 * Initialize the touchscreen driver and register it with
 * LVGL.
 */
void touch_init()
{
#ifdef USE_ARDUINO
    ts.start();
    touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read);
#endif
}

/**
 * Callback function to open the Configurator screen.
 */
void open_config_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        config.open((Panel *)lv_event_get_user_data(e));
    }
}

void close_config_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        config.close();
        lv_screen_load(screen_1.screen);
    }
}

void setup()
{
#ifdef USE_ARDUINO
    Serial.begin(115200);
#endif

    eeprom.load_config();

    lv_init();

#ifdef USE_ARDUINO
    can_init();
#endif

#ifdef USE_ARDUINO
    display_init();
    touch_init();
#else
    hal_setup();
#endif

    setup_init(eeprom);

    lv_screen_load(screen_1.screen);

    lv_obj_add_event_cb(
        panel_1.btn, open_config_cb, LV_EVENT_CLICKED, &panel_1);
    lv_obj_add_event_cb(
        panel_2.btn, open_config_cb, LV_EVENT_CLICKED, &panel_2);
    lv_obj_add_event_cb(
        panel_3.btn, open_config_cb, LV_EVENT_CLICKED, &panel_3);
    lv_obj_add_event_cb(
        panel_4.btn, open_config_cb, LV_EVENT_CLICKED, &panel_4);
    lv_obj_add_event_cb(
        panel_5.btn, open_config_cb, LV_EVENT_CLICKED, &panel_5);

    lv_obj_add_event_cb(config.done, close_config_cb, LV_EVENT_CLICKED, NULL);

#ifdef USE_ARDUINO
    lv_tick_set_cb(millis);
#endif
}

void loop()
{
    lv_task_handler();

    screen_1.update();

#ifdef USE_ARDUINO
    can_loop();
#endif
}

#ifndef USE_ARDUINO
int main()
{
    setup();
    hal_loop();
    return 0;
}
#endif
