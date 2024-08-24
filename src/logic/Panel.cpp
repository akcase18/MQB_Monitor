#include "Panel.hpp"

/****************
 * Constructors *
 ****************/

Panel::Panel()
{
    /* Pls don't use this */
}

Panel::Panel(Information *info, lv_obj_t *panel_button, lv_obj_t *panel_button_data, lv_obj_t *panel_label)
{
    panel_info = info;
    btn = panel_button;
    data_btn = panel_button_data;
    label = panel_label;
}

Panel::Panel(Information *info, lv_obj_t *panel_button, lv_obj_t *panel_button_data, lv_obj_t *panel_label, lv_style_t *panel_button_style, lv_style_t *panel_label_style)
{
    panel_info = info;
    btn = panel_button;
    data_btn = panel_button_data;
    label = panel_label;
    style_btn = panel_button_style;
    style_label = panel_label_style;
}

/******************************
 * Public Getters and Setters *
 ******************************/

void Panel::set_styles(lv_style_t *btn_style, lv_style_t *btn_style_pressed,  lv_style_t *label_style)
{
    set_button_style(btn_style);
    set_label_style(label_style);
}

void Panel::set_callback(lv_obj_t *object, void (*function)(lv_event_t *e), lv_event_code_t state, void *user_data)
{
    lv_obj_add_event_cb(object, function, state, user_data);
}

/*******************************
 * Private Getters and Setters *
 *******************************/

void Panel::set_button_style(lv_style_t *style)
{
    style_btn = style;
}

void Panel::set_button_style_pressed(lv_style_t *style)
{
    style_btn_pressed = style;
}

void Panel::set_label_style(lv_style_t *style)
{
    style_label = style;
}