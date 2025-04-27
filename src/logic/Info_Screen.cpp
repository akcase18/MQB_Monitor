#include "Screen.hpp"

/****************
 * Constructors *
 ****************/

Info_Screen::Info_Screen()
{
}


/********************
 * Public Functions *
 ********************/

void Info_Screen::init(Panel **panel_list, uint8_t num_panels)
{
    this->panels = panel_list;
    this->num_panels = num_panels;

    this->screen = lv_obj_create(NULL);
    lv_obj_set_size(this->screen, LCD_HORZ, LCD_VERT);
    lv_obj_center(this->screen);
    lv_obj_add_style(this->screen, &style_screen, 0);

    lv_obj_set_flex_flow(this->screen, LV_FLEX_FLOW_ROW_WRAP);

    for (uint8_t i = 0; i < num_panels; i++)
    {
        lv_obj_set_parent(panels[i]->btn, this->screen);
    }

    /* Temporary */
    lv_obj_set_state(panels[0]->btn, LV_STATE_USER_1, true);
    lv_obj_set_state(panels[1]->btn, LV_STATE_USER_1, true);
    lv_obj_set_state(panels[2]->btn, LV_STATE_USER_1, true);
    lv_obj_set_state(panels[3]->btn, LV_STATE_USER_2, true);
    lv_obj_set_state(panels[4]->btn, LV_STATE_USER_2, true);
}