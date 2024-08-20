#include "gui.hpp"

/*************************
 * Panel Object Creation *
 *************************/

Panel panel_tl = Panel(&coolant_temp, btn_tl, data_tl, label_tl, &style_btn_top, &style_label_top);      // Top left Panel object
Panel panel_tc = Panel(&engine_oil_temp, btn_tc, data_tc, label_tc, &style_btn_top, &style_label_top);   // Top center Panel object
Panel panel_tr = Panel(&intake_air_temp, btn_tr, data_tr, label_tr, &style_btn_top, &style_label_top);   // Top right Panel object
Panel panel_bl = Panel(&intake_pressure, btn_bl, data_bl, label_bl, &style_btn_btm, &style_label_btm);   // Bottom left Panel object
Panel panel_br = Panel(&throttle_position, btn_br, data_br, label_br, &style_btn_btm, &style_label_btm); // Bottom right Panel object

/**************************
 * Layout Object Creation *
 **************************/

Layout layout_1 = Layout(&panel_tl, &panel_tc, &panel_tr, &panel_bl, &panel_br); // Main layout creation

void setup()
{
    style_init();
}

void loop()
{
}