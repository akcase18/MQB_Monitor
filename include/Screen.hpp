#pragma once

/**
 * Base class. Will have a main screen (name TBD) that will
 * hold the Panels and a configuration screen as child classes.
 */

#include "Panel.hpp"

constexpr uint16_t LCD_HORZ = 480;
constexpr uint16_t LCD_VERT = 320;

class Screen
{
public:
    lv_obj_t *screen;

    Screen();
};

class Info_Screen : public Screen
{
public:
    Panel **panels;
    uint8_t num_panels;

    Info_Screen(Panel **panel_list, uint8_t num_panels);
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