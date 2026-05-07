#include "Screen.hpp"

/*==============================================================================
 Constructors
 =============================================================================*/

Screen::Screen() {};

/*==============================================================================
 Public Functions
 =============================================================================*/

void Screen::init()
{
    this->screen = lv_obj_create(NULL);
    lv_obj_set_size(this->screen, LCD_HORZ, LCD_VERT);
    lv_obj_center(this->screen);
    lv_obj_add_style(this->screen, &style_screen, 0);
}