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
    button = panel_button;
    button_data = panel_button_data;
    label = panel_label;
    button_style = panel_button_style;
    label_style = panel_label_style;
}

/******************************
 * Public Getters and Setters *
 ******************************/

void Panel::set_button_style(lv_style_t *style)
{
    button_style = style;   
}
lv_style_t *Panel::get_button_style()
{
    return button_style;
}

void Panel::set_label_style(lv_style_t *style)
{
    label_style = style;
}
lv_style_t *Panel::get_label_style()
{
    return label_style;
}