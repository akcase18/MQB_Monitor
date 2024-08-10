#include "Panel.hpp"

/****************
 * Constructors *
 ****************/

Panel::Panel()
{
    /* Pls don't use this */
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

void Panel::set_button_style(lv_style_t *style)
{
    style_btn = style;
}
lv_style_t *Panel::get_button_style()
{
    return style_btn;
}

void Panel::set_label_style(lv_style_t *style)
{
    style_label = style;
}
lv_style_t *Panel::get_label_style()
{
    return style_label;
}