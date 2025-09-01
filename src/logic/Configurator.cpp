#include "Screen.hpp"

/****************
 * Constructors *
 ****************/

Configurator::Configurator(Info **info_list, uint8_t total_num_info)
    : infos(info_list), total_num_info(total_num_info)
{
}

/********************
 * Public Functions *
 ********************/

void Configurator::init()
{
    this->screen = lv_obj_create(NULL);
    lv_obj_set_size(this->screen, LCD_HORZ, LCD_VERT);
    lv_obj_center(this->screen);
    lv_obj_add_style(this->screen, &style_screen, 0);

    this->info_roller = lv_roller_create(this->screen);
    lv_obj_add_style(this->info_roller, &style_config_roller, 0);
    String roller_options;
    for (uint i = 0; i < total_num_info; i++)
    {
        roller_options += this->infos[i]->get_name_and_label();
        if (i != (total_num_info - 1))
        {
            roller_options += "\n";
        }
    }
    lv_roller_set_options(this->info_roller, roller_options.c_str(), LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(this->info_roller, 4);

    this->done = lv_btn_create(this->screen);
    lv_obj_add_style(this->done, &style_config_done, 0);
    this->done_label = lv_label_create(this->done);
    lv_obj_center(this->done_label);
    lv_label_set_text(this->done_label, LV_SYMBOL_OK);
}

void Configurator::open(Panel *panel)
{
    lv_screen_load(this->screen);
    lv_roller_set_selected(this->info_roller, panel->info->uid, LV_ANIM_OFF);
    this->selected_panel = panel;
}

void Configurator::close()
{
    this->selected_panel->set_info(this->infos[lv_roller_get_selected(this->info_roller)]);
}