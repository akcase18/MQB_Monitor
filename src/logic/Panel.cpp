#include "Panel.hpp"

/****************
 * Constructors *
 ****************/

Panel::Panel()
{
    /* Pretty please don't use this */
}

Panel::Panel(Information *info)
{
    set_info(info);
}

Panel::Panel(Information *info, lv_obj_t *btn, lv_obj_t *data, lv_obj_t *label)
{
    set_info(info);
    set_btn(btn);
    set_data(data);
    set_label(label);
}

Panel::Panel(Information *info, lv_obj_t *btn, lv_obj_t *data, lv_obj_t *label, lv_style_t style_btn, lv_style_t style_btn_pressed, lv_style_t style_label)
{
    set_info(info);
    set_btn(btn);
    set_data(data);
    set_label(label);
    set_btn_style(style_btn);
    set_btn_pressed_style(style_btn_pressed);
    set_label_style(style_label);
}

/******************************
 * Public Getters and Setters *
 ******************************/

void Panel::set_styles(lv_style_t style_btn, lv_style_t style_btn_pressed, lv_style_t style_label)
{
    set_btn_style(style_btn);
    set_btn_pressed_style(style_btn_pressed);
    set_label_style(style_label);
}

/***************************
 * Public Helper Functions *
 ***************************/

/*******************************
 * Private Getters and Setters *
 *******************************/

void Panel::set_info(Information *info)
{
    _panel_info = info;
}

void Panel::set_btn(lv_obj_t *btn)
{
    _btn = btn;
}

void Panel::set_data(lv_obj_t *data)
{
    _data = data;
}

void Panel::set_label(lv_obj_t *label)
{
    _label = label;
}

void Panel::set_btn_style(lv_style_t style_btn)
{
    _style_btn = style_btn;
}

void Panel::set_btn_pressed_style(lv_style_t style_btn_pressed)
{
    _style_btn_pressed = _style_btn_pressed;
}

void Panel::set_label_style(lv_style_t style_label)
{
    _style_label = style_label;
}

/****************************
 * Private Helper Functions *
 ****************************/