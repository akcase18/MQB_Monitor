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

/****************************
 * Private Helper Functions *
 ****************************/