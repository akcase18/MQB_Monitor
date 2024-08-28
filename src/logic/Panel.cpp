#include "Panel.hpp"

/*******************
 * Button Creation *
 *******************/

lv_obj_t *btn_tl; // Top left button object
lv_obj_t *btn_tc; // Top center button object
lv_obj_t *btn_tr; // Top right button object
lv_obj_t *btn_bl; // Bottom left button object
lv_obj_t *btn_br; // Bottom right button objet

/************************
 * Button Data Creation *
 ************************/

lv_obj_t *data_tl; // Top left data object
lv_obj_t *data_tc; // Top center data object
lv_obj_t *data_tr; // Top right data object
lv_obj_t *data_bl; // Bottom left data object
lv_obj_t *data_br; // Bottom right data object

/******************
 * Label Creation *
 ******************/

lv_obj_t *label_tl; // Top left label object
lv_obj_t *label_tc; // Top center label object
lv_obj_t *label_tr; // Top right label object
lv_obj_t *label_bl; // Bottom left label object
lv_obj_t *label_br; // Bottom right label object

/***************************
 * Panel Object Definition *
 ***************************/

DMAMEM Panel panel_tl = Panel(&coolant_temp, btn_tl, data_tl, label_tl);      // Top left Panel object
DMAMEM Panel panel_tc = Panel(&engine_oil_temp, btn_tc, data_tc, label_tc);   // Top center Panel object
DMAMEM Panel panel_tr = Panel(&intake_air_temp, btn_tr, data_tr, label_tr);   // Top right Panel object
DMAMEM Panel panel_bl = Panel(&intake_pressure, btn_bl, data_bl, label_bl);   // Bottom left Panel object
DMAMEM Panel panel_br = Panel(&throttle_position, btn_br, data_br, label_br); // Bottom right Panel object


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