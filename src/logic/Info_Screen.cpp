#include "Screen.hpp"

/****************
 * Constructors *
 ****************/

Info_Screen::Info_Screen(Panel **panel_list, uint8_t total_num_panels)
    : panels(panel_list),
      total_num_panels(total_num_panels)
{
}


/********************
 * Public Functions *
 ********************/

void Info_Screen::init(uint8_t num_panels)
{
    this->num_panels = num_panels;

    this->screen = lv_obj_create(NULL);
    lv_obj_set_size(this->screen, LCD_HORZ, LCD_VERT);
    lv_obj_center(this->screen);
    lv_obj_add_style(this->screen, &style_screen, 0);

    lv_obj_set_flex_flow(this->screen, LV_FLEX_FLOW_ROW_WRAP);

    for (uint8_t i = 0; i < num_panels; i++)
    {
        panels[i]->init(this->screen);
        if (i == (int)((num_panels / 2) + 1))
        {
            lv_obj_add_flag(panels[i]->btn, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
        }
        lv_obj_set_parent(panels[i]->btn, this->screen);
        lv_obj_set_height(panels[i]->btn, 100);
    }

    /* Temporary */
    panels[0]->set_sm_panel();
    panels[1]->set_sm_panel();
    panels[2]->set_sm_panel();
    panels[3]->set_lg_panel();
    panels[4]->set_lg_panel();
}