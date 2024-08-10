#include "Configurator.hpp"

/****************
 * Constructors *
 ****************/

Configurator::Configurator()
{

}

/***************************
 * Public Helper Functions *
 ***************************/

void Configurator::show_config_window()
{
    window = lv_obj_create(lv_scr_act());
    lv_obj_set_size(window, 480, 320);
    lv_obj_center(window);
    lv_obj_add_style(window, style_window, 0);
    /* Turn off scrolling */
    lv_obj_set_scrollbar_mode(window, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(window, LV_OBJ_FLAG_SCROLLABLE);
    /* Add elements to window */
}

/****************************
 * Private Helper Functions *
 ****************************/

void Configurator::config_style_init()
{

}