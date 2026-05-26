#include "Screen.hpp"

static void close_warning_settings_cb(lv_event_t *e);
static void switch_warning_active_cb(lv_event_t *e);
static void switch_hi_warning_cb(lv_event_t *e);
static void switch_lo_warning_cb(lv_event_t *e);
static void slider_cb(lv_event_t *e);

/*==============================================================================
 Constructors
 =============================================================================*/

WarningConfig::WarningConfig()
{
}

/*==============================================================================
 Public Functions
 =============================================================================*/

void WarningConfig::init()
{
    /* Screen Setup */
    this->screen = create_new_screen();

    /* Use Warnings */
    this->use_warning_sel = lv_checkbox_create(this->screen);
    lv_checkbox_set_text(this->use_warning_sel, "Warning Active");
    lv_obj_align(this->use_warning_sel, LV_ALIGN_TOP_LEFT, 20, 20);
    lv_obj_add_style(this->use_warning_sel, &style_config_checkbox, 0);
    lv_obj_add_style(
        this->use_warning_sel, &style_config_checkbox_sel, LV_STATE_CHECKED);
    lv_obj_add_event_cb(this->use_warning_sel,
                        switch_warning_active_cb,
                        LV_EVENT_CLICKED,
                        this);

    /* Use High Warning */
    this->use_hi_sel = lv_checkbox_create(this->screen);
    lv_checkbox_set_text(this->use_hi_sel, "High Warning:");
    lv_obj_align(this->use_hi_sel, LV_ALIGN_TOP_LEFT, 20, 60);
    lv_obj_add_state(this->use_hi_sel, LV_STATE_DISABLED);
    lv_obj_add_style(this->use_hi_sel, &style_config_checkbox, 0);
    lv_obj_add_style(
        this->use_hi_sel, &style_config_checkbox_sel, LV_STATE_CHECKED);
    lv_obj_add_style(
        this->use_hi_sel, &style_config_disable, LV_STATE_DISABLED);
    lv_obj_add_event_cb(
        this->use_hi_sel, switch_hi_warning_cb, LV_EVENT_CLICKED, this);

    /* High Warning Label */
    this->hi_label = lv_label_create(this->screen);
    lv_label_set_text(this->hi_label, "--");
    lv_obj_center(this->hi_label);
    lv_obj_align(this->hi_label, LV_ALIGN_TOP_LEFT, 100, 60);
    lv_obj_add_state(this->hi_label, LV_STATE_DISABLED);
    lv_obj_set_style_text_font(this->hi_label, &vw_18, 0);
    lv_obj_add_style(this->hi_label, &style_config_disable, LV_STATE_DISABLED);

    /* High Warning Slider */
    this->hi_slider = lv_slider_create(this->screen);
    lv_obj_align(this->hi_slider, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_add_flag(this->hi_slider, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_state(this->hi_slider, LV_STATE_DISABLED);
    lv_obj_add_style(this->hi_slider, &style_slider_disable, LV_STATE_DISABLED);
    lv_obj_add_style(this->hi_slider,
                     &style_slider_disable,
                     LV_PART_KNOB | LV_STATE_DISABLED);
    lv_slider_set_range(this->hi_slider, 0, 100);

    /* Use Low Warning */
    this->use_lo_sel = lv_checkbox_create(this->screen);
    lv_checkbox_set_text(this->use_lo_sel, "Low Warning:");
    lv_obj_align(this->use_lo_sel, LV_ALIGN_TOP_LEFT, 20, 140);
    lv_obj_add_state(this->use_lo_sel, LV_STATE_DISABLED);
    lv_obj_add_style(this->use_lo_sel, &style_config_checkbox, 0);
    lv_obj_add_style(
        this->use_lo_sel, &style_config_checkbox_sel, LV_STATE_CHECKED);
    lv_obj_add_style(
        this->use_lo_sel, &style_config_disable, LV_STATE_DISABLED);
    lv_obj_add_event_cb(
        this->use_lo_sel, switch_lo_warning_cb, LV_EVENT_CLICKED, this);

    /* Low Warning Slider */
    this->lo_slider = lv_slider_create(this->screen);
    lv_obj_align(this->lo_slider, LV_ALIGN_TOP_MID, 0, 180);
    lv_obj_add_flag(this->lo_slider, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_state(this->lo_slider, LV_STATE_DISABLED);
    lv_obj_add_style(this->lo_slider, &style_slider_disable, LV_STATE_DISABLED);
    lv_obj_add_style(this->lo_slider,
                     &style_slider_disable,
                     LV_PART_KNOB | LV_STATE_DISABLED);
    lv_slider_set_range(this->lo_slider, 0, 100);

    /* Done Button */
    done_btn_t done_btn = create_done_btn(this->screen);
    this->done          = done_btn.btn;
    this->done_label    = done_btn.label;

    lv_obj_add_event_cb(
        this->done, close_warning_settings_cb, LV_EVENT_CLICKED, this);
}

void WarningConfig::open(Info *info, lv_obj_t *return_screen)
{
    this->info          = info;
    this->return_screen = return_screen;

    lv_screen_load(this->screen);
}

void WarningConfig::close()
{
    /* Do nothing */
}

/*==============================================================================
 Helper Functions
 =============================================================================*/

static void close_warning_settings_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        WarningConfig *temp = (WarningConfig *)lv_event_get_user_data(e);

        temp->close();

        if (temp != NULL)
        {
            lv_screen_load(temp->return_screen);
        }
    }
}

static void switch_warning_active_cb(lv_event_t *e)
{
    lv_obj_t      *obj  = lv_event_get_target_obj(e);
    WarningConfig *temp = (WarningConfig *)lv_event_get_user_data(e);

    if (obj == temp->use_warning_sel)
    {
        if (lv_obj_has_state(obj, LV_STATE_CHECKED))
        {
            lv_obj_remove_state(temp->use_hi_sel, LV_STATE_DISABLED);
            lv_obj_remove_state(temp->use_lo_sel, LV_STATE_DISABLED);
            if (lv_obj_has_state(temp->use_hi_sel, LV_STATE_CHECKED))
            {
                lv_obj_remove_state(temp->hi_slider, LV_STATE_DISABLED);
            }
            if (lv_obj_has_state(temp->use_lo_sel, LV_STATE_CHECKED))
            {
                lv_obj_remove_state(temp->lo_slider, LV_STATE_DISABLED);
            }
        }
        else
        {
            lv_obj_add_state(temp->use_hi_sel, LV_STATE_DISABLED);
            lv_obj_add_state(temp->use_lo_sel, LV_STATE_DISABLED);
            lv_obj_add_state(temp->hi_slider, LV_STATE_DISABLED);
            lv_obj_add_state(temp->lo_slider, LV_STATE_DISABLED);
        }
    }
}

static void switch_hi_warning_cb(lv_event_t *e)
{
    lv_obj_t      *obj  = lv_event_get_target_obj(e);
    WarningConfig *temp = (WarningConfig *)lv_event_get_user_data(e);

    if (obj == temp->use_hi_sel)
    {
        if (lv_obj_has_state(obj, LV_STATE_CHECKED))
        {
            lv_obj_remove_state(temp->hi_slider, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_add_state(temp->hi_slider, LV_STATE_DISABLED);
        }
    }
}

static void switch_lo_warning_cb(lv_event_t *e)
{
    lv_obj_t      *obj  = lv_event_get_target_obj(e);
    WarningConfig *temp = (WarningConfig *)lv_event_get_user_data(e);

    if (obj == temp->use_lo_sel)
    {
        if (lv_obj_has_state(obj, LV_STATE_CHECKED))
        {
            lv_obj_remove_state(temp->lo_slider, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_add_state(temp->lo_slider, LV_STATE_DISABLED);
        }
    }
}

static void slider_cb(lv_event_t *e)
{
}