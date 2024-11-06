#include "Panel.hpp"

/***************************
 * Panel Object Definition *
 ***************************/

DMAMEM Panel panel_tl = Panel(&throttle_position);
DMAMEM Panel panel_tc = Panel(&misfire_sum);
DMAMEM Panel panel_tr = Panel(&intake_air_temp);
DMAMEM Panel panel_bl = Panel(&intake_pressure);
DMAMEM Panel panel_br = Panel(&engine_oil_temp);

/****************
 * Constructors *
 ****************/

Panel::Panel(Information *info)
{
    set_info(info);
}

/******************************
 * Public Getters and Setters *
 ******************************/

/***************************
 * Public Helper Functions *
 ***************************/

/*******************************
 * Private Getters and Setters *
 *******************************/

void Panel::set_info(Information *info)
{
    panel_info = info;
}

void Panel::set_btn(lv_obj_t *btn)
{
    btn = btn;
}

void Panel::set_data(lv_obj_t *data)
{
    data = data;
}

void Panel::set_label(lv_obj_t *label)
{
    label = label;
}

void Panel::set_btn_style(lv_style_t style_btn)
{
    style_btn = style_btn;
}

void Panel::set_btn_pressed_style(lv_style_t style_btn_pressed)
{
    style_btn_pressed = style_btn_pressed;
}

void Panel::set_label_style(lv_style_t style_label)
{
    style_label = style_label;
}

/****************************
 * Private Helper Functions *
 ****************************/