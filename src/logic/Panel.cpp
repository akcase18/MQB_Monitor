#include "Panel.hpp"

/****************
 * Constructors *
 ****************/

Panel::Panel()
{
    panel_info = Information();
}

Panel::Panel(Information *info)
{
    panel_info = Information(info);
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