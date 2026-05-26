#include "Panel.hpp"

static uint32_t panel_unique_id = 0;

/*==============================================================================
 Constructors
 =============================================================================*/

Panel::Panel()
{
    uid = panel_unique_id++;
}

/*==============================================================================
 Public Functions
 =============================================================================*/

void Panel::init(lv_obj_t *parent)
{
    // Create the button
    this->btn = lv_btn_create(parent);
    lv_obj_add_style(this->btn, &style_panel_btn, 0);
    lv_obj_add_style(
        this->btn, &style_panel_btn_sm, LV_STATE_USER_1); // Small Panel
    lv_obj_add_style(
        this->btn, &style_panel_btn_lg, LV_STATE_USER_2); // Large Panel
    lv_obj_add_style(this->btn, &style_btn_pressed, LV_STATE_PRESSED);

    // Create the label for the data
    this->data = lv_label_create(btn);
    lv_obj_add_style(this->data, &style_panel_data, 0);
    lv_obj_add_style(
        this->data, &style_panel_data_sm, LV_STATE_USER_1); // Small Panel
    lv_obj_add_style(
        this->data, &style_panel_data_lg, LV_STATE_USER_2); // Large Panel
    lv_obj_add_style(this->data, &style_panel_data_lo_warn, LV_STATE_USER_3);
    lv_obj_add_style(this->data, &style_panel_data_hi_warn, LV_STATE_USER_4);
    lv_obj_center(this->data);

    // Create the label for the name
    this->label = lv_label_create(btn);
    lv_obj_add_style(this->label, &style_panel_label, 0);
    lv_obj_add_style(
        this->label, &style_panel_label_sm, LV_STATE_USER_1); // Small Panel
    lv_obj_add_style(
        this->label, &style_panel_label_lg, LV_STATE_USER_2); // Large Panel
    lv_obj_set_align(this->label, LV_ALIGN_BOTTOM_MID);

    this->initialized = true;
}

void Panel::set_info(Info *new_info)
{
    info = new_info;

    char data_str[MAX_DATA_LEN];
    info->get_data_str(data_str, sizeof(data_str));
    lv_label_set_text(data, data_str);

    char name_and_label[MAX_NAME_AND_LABEL_LEN];
    info->get_name_and_label(name_and_label, sizeof(name_and_label));
    lv_label_set_text(label, name_and_label);
}

void Panel::update()
{
    if (this->info->high_warning_active())
    {
        lv_obj_remove_state(this->data, LV_STATE_USER_3);
        lv_obj_add_state(this->data, LV_STATE_USER_4);
    }
    else if (this->info->low_warning_active())
    {
        lv_obj_add_state(this->data, LV_STATE_USER_3);
        lv_obj_remove_state(this->data, LV_STATE_USER_4);
    }
    else
    {
        lv_obj_remove_state(this->data, LV_STATE_USER_3);
        lv_obj_remove_state(this->data, LV_STATE_USER_4);
    }

    char data_str[MAX_DATA_LEN];
    info->get_data_str(data_str, sizeof(data_str));
    lv_label_set_text(data, data_str);
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
