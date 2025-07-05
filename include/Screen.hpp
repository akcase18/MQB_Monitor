#pragma once

/**
 * Base class. Will have a main screen (name TBD) that will
 * hold the Panels and a configuration screen as child classes.
 */

#include "etl/queue.h"
#include "Panel.hpp"

constexpr uint16_t LCD_HORZ = 480;
constexpr uint16_t LCD_VERT = 320;

class Screen
{
public:
    lv_obj_t *screen;

    Screen();

    void init();
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
    lv_obj_t *flex_col_1;
    lv_obj_t *flex_col_2;
    lv_obj_t *info_sel; // Roller object
    lv_obj_t *radio_i;
    lv_obj_t *radio_m;

    Configurator();
};

extern etl::queue<Request *, 64> can_queue;