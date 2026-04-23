#pragma once

/**
 * Base class. Will have a main screen (name TBD) that will
 * hold the Panels and a configuration screen as child classes.
 */

#include "etl/queue.h"
#include "Panel.hpp"

#define PWM_PIN 3
#define MAX_BRIGHTNESS 256
#define DFLT_DAY_BRIGHTNESS 100
#define DFLT_NIGHT_BRIGHTNESS 30

constexpr uint16_t LCD_HORZ = 480;
constexpr uint16_t LCD_VERT = 320;

class Screen
{
public:
    lv_obj_t *screen;

    Screen();

    void init();
};

class Settings_Screen : public Screen
{
public:
    enum BrightMode
    {
        BRIGHT_DAY,
        BRIGHT_NIGHT
    };

    struct Brightness
    {
        uint8_t day;
        uint8_t night;
    };

    Brightness brightness;

    lv_obj_t *day_icon;
    lv_obj_t *day_slider;
    lv_obj_t *night_icon;
    lv_obj_t *night_slider;

    Settings_Screen();

    void init();

    void set_brightness(BrightMode mode);
    void set_brightness(BrightMode mode, uint8_t value);
};

class Info_Screen : public Screen
{
public:
    Panel **panels;
    uint8_t total_num_panels; // Maximum number of Panels
    uint8_t num_panels;       // Number of Panels displayed in this Screen

    Info_Screen(Panel **panel_list, uint8_t total_num_panels);

    void init(uint8_t num_panels);

    void update();
};

class Configurator : public Screen
{
public:
    Info **infos;
    uint8_t total_num_info;

    Panel *selected_panel;

    lv_obj_t *flex_col_1;
    lv_obj_t *flex_col_2;
    lv_obj_t *info_roller; // Roller object
    lv_obj_t *radio_i;
    lv_obj_t *radio_m;
    lv_obj_t *done;
    lv_obj_t *done_label;

    Configurator(Info **info_list, uint8_t total_num_info);

    void init();

    void open(Panel *panel);

    void close();
};

extern etl::queue<Request *, 64> can_queue;