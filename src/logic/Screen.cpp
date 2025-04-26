#include "Screen.hpp"

/****************
 * Constructors *
 ****************/

Screen::Screen()
{
    screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, LCD_HORZ, LCD_VERT);
    lv_obj_center(screen);
    lv_obj_add_style(screen, &style_screen, 0);
};

Info_Screen::Info_Screen(Panel **panel_list, uint8_t num_panels)
    : panels(panel_list),
      num_panels(num_panels)
{
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_ROW_WRAP);

    for (uint8_t i = 0; i < num_panels; i++)
    {
        lv_obj_set_parent(panels[i]->btn, screen);
    }

    /* Temporary */
    lv_obj_set_state(panels[0]->btn, LV_STATE_USER_1, true);
    lv_obj_set_state(panels[1]->btn, LV_STATE_USER_1, true);
    lv_obj_set_state(panels[2]->btn, LV_STATE_USER_1, true);
    lv_obj_set_state(panels[3]->btn, LV_STATE_USER_2, true);
    lv_obj_set_state(panels[4]->btn, LV_STATE_USER_2, true);
}

Configurator::Configurator()
{

}