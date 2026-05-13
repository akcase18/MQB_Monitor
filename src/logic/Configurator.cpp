#include "Screen.hpp"

static void radio_btn_sel_cb(lv_event_t *e);

/*==============================================================================
 Constructors
 =============================================================================*/

Configurator::Configurator(Info **info_list, uint8_t total_num_info)
    : infos(info_list)
    , total_num_info(total_num_info)
{
}

/*==============================================================================
 Public Functions
 =============================================================================*/

void Configurator::init()
{
    /* Screen Setup */
    this->screen = lv_obj_create(NULL);
    lv_obj_set_size(this->screen, LCD_HORZ, LCD_VERT);
    lv_obj_center(this->screen);
    lv_obj_add_style(this->screen, &style_screen, 0);

    /* Info Selection */
    this->info_roller = lv_roller_create(this->screen);
    lv_obj_add_style(this->info_roller, &style_config_roller, 0);

    char roller_options[512] = "";
    char line[MAX_NAME_AND_LABEL_LEN];
    for (uint8_t i = 0; i < total_num_info; i++)
    {
        this->infos[i]->get_name_and_label(line, sizeof(line));
        strncat(roller_options,
                line,
                sizeof(roller_options) - strlen(roller_options) - 1);

        if (i != (total_num_info - 1))
        {
            strncat(roller_options,
                    "\n",
                    sizeof(roller_options) - strlen(roller_options) - 1);
        }
    }
    lv_roller_set_options(
        this->info_roller, roller_options, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(this->info_roller, 4);

    /* Unit Type Selection */
    this->radio_i = lv_checkbox_create(this->screen);
    lv_checkbox_set_text(this->radio_i, "Imperial");
    lv_obj_add_style(this->radio_i, &style_config_radio_btn, 0);
    lv_obj_add_style(
        this->radio_i, &style_config_radio_btn_sel, LV_STATE_CHECKED);
    lv_obj_align(this->radio_i, LV_ALIGN_TOP_RIGHT, 0, 40);
    lv_obj_add_event_cb(
        this->radio_i, radio_btn_sel_cb, LV_EVENT_CLICKED, this);

    this->radio_m = lv_checkbox_create(this->screen);
    lv_checkbox_set_text(this->radio_m, "Metric");
    lv_obj_add_style(this->radio_m, &style_config_radio_btn, 0);
    lv_obj_add_style(
        this->radio_m, &style_config_radio_btn_sel, LV_STATE_CHECKED);
    lv_obj_align(this->radio_m, LV_ALIGN_TOP_RIGHT, 0, 80);
    lv_obj_add_event_cb(
        this->radio_m, radio_btn_sel_cb, LV_EVENT_CLICKED, this);

    /* More Settings */
    this->settings = lv_btn_create(this->screen);
    lv_obj_add_style(this->settings, &style_config_settings, 0);
    this->settings_label = lv_label_create(this->settings);
    lv_obj_center(this->settings_label);
    lv_label_set_text(this->settings_label, LV_SYMBOL_SETTINGS);

    /* Done Button */
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
    if (panel->info->get_metric())
    {
        lv_obj_add_state(this->radio_m, LV_STATE_CHECKED);
        lv_obj_remove_state(this->radio_i, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_add_state(this->radio_i, LV_STATE_CHECKED);
        lv_obj_remove_state(this->radio_m, LV_STATE_CHECKED);
    }
    this->selected_panel = panel;
}

void Configurator::close()
{
    this->selected_panel->set_info(
        this->infos[lv_roller_get_selected(this->info_roller)]);
    this->selected_panel->info->set_metric(lv_obj_get_state(this->radio_m)
                                           == LV_STATE_CHECKED);
    refresh_request_timers();
    eeprom.save_panel(
        this->selected_panel->panel_num,
        { (uint8_t)this->selected_panel->info->uid,
          this->selected_panel->info->get_metric() ? METRIC : IMPERIAL });
}

/*==============================================================================
 Private Functions
 =============================================================================*/

static void radio_btn_sel_cb(lv_event_t *e)
{
    lv_obj_t     *obj  = lv_event_get_target_obj(e);
    Configurator *temp = (Configurator *)lv_event_get_user_data(e);

    if (obj == temp->radio_i)
    {
        lv_obj_add_state(temp->radio_i, LV_STATE_CHECKED);
        lv_obj_remove_state(temp->radio_m, LV_STATE_CHECKED);
    }
    else if (obj == temp->radio_m)
    {
        lv_obj_add_state(temp->radio_m, LV_STATE_CHECKED);
        lv_obj_remove_state(temp->radio_i, LV_STATE_CHECKED);
    }
    else
    {
        /* Do nothing */
    }
}