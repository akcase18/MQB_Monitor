#include "Configurator.hpp"

/**********************************
 * Configurator Object Definition *
 **********************************/

// DMAMEM Configurator config = Configurator();

/****************
 * Constructors *
 ****************/

Configurator::Configurator() {}

/***************************
 * Public Helper Functions *
 ***************************/

void Configurator::init_configurator()
{
    style_init();
    init_window();
    init_done_btn();
    init_radio_btns();
}

void Configurator::open_window()
{
    lv_screen_load(window);
}

void Configurator::close_window()
{
}

void Configurator::set_callback(lv_obj_t *object, void (*function)(lv_event_t *e), lv_event_code_t state, void *user_data)
{
}

/****************************
 * Private Helper Functions *
 ****************************/

void Configurator::style_init()
{
}

void Configurator::init_window()
{
}

void Configurator::init_done_btn()
{
}

void Configurator::init_radio_btns()
{
}

Information *Configurator::get_info_from_drop_down()
{
}