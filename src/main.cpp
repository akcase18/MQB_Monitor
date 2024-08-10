#include "gui.hpp"

/*******************************
 * Information Object Creation *
 *******************************/

/***** Performance Metrics *****/
Information coolant_temp = Information("Coolant Temp", 0xF4, 0x05, -40, NO_SCALE, Information::TEMP, 0, 0, 2500);
Information engine_oil_temp = Information("Engine Oil Temp", 0x20, 0x2F, -2731.4, 0.1, Information::TEMP, 0, 0, 1000);
Information intake_air_temp = Information("Intake Air Temp", 0xF4, 0x0F, -40, NO_SCALE, Information::TEMP, 0, 0, 100);
Information charge_pressure = Information("Charge Pressure", 0x20, 0x2A, NO_OFFSET, 0.001, Information::PRESSURE, 2, 1, 100);
Information intake_pressure = Information("Intake Pressure", 0x3E, 0x70, NO_OFFSET, 0.001, Information::PRESSURE, 2, 1, MAX_REFRESH);
Information ambient_pressure = Information("Ambient Pressure", 0x13, 0xCA, NO_OFFSET, 0.001, Information::PRESSURE, 2, 1, 1000);
Information lambda_actual = Information("Lambda (actual)", 0x10, 0xC0, NO_OFFSET, (1 / 1024), Information::NONE, 1, 1, MAX_REFRESH);
Information lambda_specified = Information("Lambda (specified)", 0x29, 0x50, NO_OFFSET, (1 / 1024), Information::NONE, 1, 1, MAX_REFRESH);
Information turbo_speed = Information("Turbo Speed", 0x10, 0x40, NO_OFFSET, 6.11, Information::RPM, 0, 0, MIN_REFRESH);

/***** Car Information *****/
Information engine_rpm = Information("Engine Speed", 0x20, 0x6E, NO_OFFSET, NO_SCALE, Information::RPM, 0, 0, MAX_REFRESH);
Information engine_torque = Information("Engine Torque", 0x43, 0x7C, NO_OFFSET, 0.1, Information::TORQUE, 0, 0, MAX_REFRESH);
Information vehicle_speed = Information("Vehicle Speed", 0x20, 0x33, NO_OFFSET, 0.01, Information::SPEED, 0, 0, 100);
Information fuel_level = Information("Fuel", 0x22, 0x06, NO_OFFSET, NO_SCALE, Information::VOLUME, 1, 1, MIN_REFRESH);
Information fuel_percent = Information("Fuel", 0xF4, 0x2F, NO_OFFSET, (1 / 2.55), Information::PERCENT, 0, 0, MIN_REFRESH);
Information throttle_position = Information("Throttle Position", 0xF4, 0x11, NO_OFFSET, (1 / 2.55), Information::PERCENT, 0, 0, MAX_REFRESH);

/***** Diag/Monitoring *****/
Information misfire_sum = Information("Misfire Count", 0x29, 0x04, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
Information misfire_cyl_1 = Information("Misfire Count Cyl 1", 0x29, 0x66, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
Information misfire_cyl_2 = Information("Misfire Count Cyl 2", 0x29, 0x67, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
Information misfire_cyl_3 = Information("Misfire Count Cyl 3", 0x29, 0x68, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
Information misfire_cyl_4 = Information("Misfire Count Cyl 4", 0x29, 0x69, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
Information timing_corr_1 = Information("Timing Corr. Cyl 1", 0x20, 0x0A, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);
Information timing_corr_2 = Information("Timing Corr. Cyl 2", 0x20, 0x0B, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);
Information timing_corr_3 = Information("Timing Corr. Cyl 3", 0x20, 0x0C, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);
Information timing_corr_4 = Information("Timing Corr. Cyl 4", 0x20, 0x0D, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);

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