#include "Panel.hpp"

static uint32_t panel_unique_id = 0;

/****************
 * Constructors *
 ****************/

Panel::Panel()
{
    uid = panel_unique_id++;
}

/********************
 * Public Functions *
 ********************/

void Panel::init(lv_obj_t *parent)
{
    this->info = info;

    // Create the button
    this->btn = lv_btn_create(parent);
    lv_obj_add_style(this->btn, &style_panel_btn, 0);
    lv_obj_add_style(this->btn, &style_panel_btn_sm, LV_STATE_USER_1); // Small Panel
    lv_obj_add_style(this->btn, &style_panel_btn_lg, LV_STATE_USER_2); // Large Panel
    lv_obj_add_style(this->btn, &style_panel_btn_pressed, LV_STATE_PRESSED);

    // Create the label for the data
    this->data = lv_label_create(btn);
    lv_obj_add_style(this->data, &style_panel_data, 0);
    lv_obj_add_style(this->data, &style_panel_data_sm, LV_STATE_USER_1); // Small Panel
    lv_obj_add_style(this->data, &style_panel_data_lg, LV_STATE_USER_2); // Large Panel
    // lv_label_set_text(this->data, info->get_data_str().c_str());
    lv_obj_center(this->data);

    // Create the label for the name
    this->label = lv_label_create(btn);
    lv_obj_add_style(this->label, &style_panel_label, 0);
    lv_obj_add_style(this->label, &style_panel_label_sm, LV_STATE_USER_1); // Small Panel
    lv_obj_add_style(this->label, &style_panel_label_lg, LV_STATE_USER_2); // Large Panel
    // lv_label_set_text(this->label, info->get_name_and_label().c_str());
    lv_obj_set_align(this->label, LV_ALIGN_BOTTOM_MID);

    this->initialized = true;
}

void Panel::set_info(Info *new_info)
{
    info = new_info;

    lv_label_set_text(data, info->get_data_str().c_str());
    lv_label_set_text(label, info->get_name_and_label().c_str());
}

void Panel::update()
{
    lv_label_set_text(data, info->get_data_str().c_str());
}

void Panel::set_sm_panel()
{
    lv_obj_set_state(this->btn, LV_STATE_USER_1, true);
    lv_obj_set_state(this->data, LV_STATE_USER_1, true);
    lv_obj_set_state(this->label, LV_STATE_USER_1, true);
}

void Panel::set_lg_panel()
{
    lv_obj_set_state(this->btn, LV_STATE_USER_2, true);
    lv_obj_set_state(this->data, LV_STATE_USER_2, true);
    lv_obj_set_state(this->label, LV_STATE_USER_2, true);
}