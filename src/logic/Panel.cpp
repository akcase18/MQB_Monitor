#include "Panel.hpp"

static uint32_t panel_unique_id = 0;

/****************
 * Constructors *
 ****************/

Panel::Panel(Info *new_info)
    : info(new_info)
{
    uid = panel_unique_id++;

    // Create the button
    btn = lv_btn_create(NULL);
    lv_obj_add_style(btn, &style_panel_btn, 0);
    lv_obj_add_style(btn, &style_panel_btn_pressed, LV_STATE_PRESSED);

    // Create the label for the data
    data = lv_label_create(btn);
    lv_obj_add_style(data, &style_panel_data, 0);
    lv_obj_add_style(data, &style_panel_data_sm, LV_STATE_USER_1); // Small Panel
    lv_obj_add_style(data, &style_panel_data_lg, LV_STATE_USER_2); // Large Panel
    lv_label_set_text(data, info->get_data_str().c_str());
    lv_obj_center(data);

    // Create the label for the name
    label = lv_label_create(btn);
    lv_obj_add_style(label, &style_panel_label, 0);
    lv_obj_add_style(label, &style_panel_label_sm, LV_STATE_USER_1); // Small Panel
    lv_obj_add_style(label, &style_panel_label_lg, LV_STATE_USER_2); // Large Panel
    lv_label_set_text(label, info->get_name_and_label().c_str());
    lv_obj_set_align(label, LV_ALIGN_BOTTOM_MID);
}

/********************
 * Public Functions *
 ********************/

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