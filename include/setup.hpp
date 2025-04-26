#ifndef SETUP_HPP
#define SETUP_HPP

#include "etl/unordered_map.h"
#include "transforms.hpp"
#include "Request.hpp"
#include "Info.hpp"
#include "Panel.hpp"
#include "Screen.hpp"

/*************
 * Constants *
 *************/

constexpr uint8_t NUM_REQS = 24;
constexpr uint8_t NUM_INFO = 22;
constexpr uint8_t NUM_PANELS = 5;

/************
 * Requests *
 ************/

/***** Performance Metrics *****/
Request cool_tmp_req = Request(0xF4, 0x05, -40, NO_SCALE);         // Coolant Temperature
Request eng_oil_tmp_req = Request(0x20, 0x2F, -2731.4, 0.1);       // Engine Oil Temperature
Request int_air_tmp_req = Request(0xF4, 0x0F, -40, NO_SCALE);      // Intake Air Temperature
Request chg_pres_req = Request(0x20, 0x2a, NO_OFFSET, 0.001);      // Charge Pressure
Request int_pres_req = Request(0x3E, 0x70, NO_OFFSET, 0.001);      // Intake Pressure
Request amb_pres_req = Request(0x13, 0xCA, NO_OFFSET, 0.0001);     // Ambient Pressure
Request lmd_act_req = Request(0x10, 0xC0, NO_OFFSET, (1 / 1024));  // Actual Lambda
Request lmd_spec_req = Request(0x29, 0x50, NO_OFFSET, (1 / 1024)); // Specified Lambda
Request trbo_spd_req = Request(0x10, 0x40, NO_OFFSET, 6.11);       // Turbo Speed

/***** Car Information *****/
Request eng_rpm_req = Request(0x20, 0x6E, NO_OFFSET, NO_SCALE);     // Engine RPM
Request eng_trq_req = Request(0x43, 0x7C, NO_OFFSET, 0.1);          // Engine Torque
Request car_spd_req = Request(0x20, 0x33, NO_OFFSET, 0.01);         // Vehicle Speed
Request fuel_lvl_req = Request(0x22, 0x06, NO_OFFSET, NO_SCALE);    // Fuel (Volume)
Request fuel_pcnt_req = Request(0xF4, 0x2F, NO_OFFSET, (1 / 2.55)); // Fuel (Percentage)
Request thrtl_pos_req = Request(0xF4, 0x11, NO_OFFSET, (1 / 2.55)); // Throttle Position

/***** Diag/Monitoring *****/
Request msfr_sum_req = Request(0x29, 0x04, NO_OFFSET, NO_SCALE); // Misfires (Sum)
Request msfr_1_req = Request(0x29, 0x66, NO_OFFSET, NO_SCALE);   // Misfires (Cylinder 1)
Request msfr_2_req = Request(0x29, 0x67, NO_OFFSET, NO_SCALE);   // Misfires (Cylinder 2)
Request msfr_3_req = Request(0x29, 0x68, NO_OFFSET, NO_SCALE);   // Misfires (Cylinder 3)
Request msfr_4_req = Request(0x29, 0x69, NO_OFFSET, NO_SCALE);   // Misfires (Cylinder 4)
Request tim_cor_1_req = Request(0x20, 0x0A, NO_OFFSET, 0.01);    // Timing Correction (Cylinder 1)
Request tim_cor_2_req = Request(0x20, 0x0B, NO_OFFSET, 0.01);    // Timing Correction (Cylinder 2)
Request tim_cor_3_req = Request(0x20, 0x0C, NO_OFFSET, 0.01);    // Timing Correction (Cylinder 3)
Request tim_cor_4_req = Request(0x20, 0x0D, NO_OFFSET, 0.01);    // Timing Correction (Cylinder 4)

// etl::unordered_map<UDS_Data, Request *, NUM_REQS> request_list;

/********
 * Info *
 ********/

/* Coolant Temp */
Request *coolant_temp_req_list[] = {&cool_tmp_req};
Info coolant_temp = Info("Coolant Temp", Unit_Type::Temp, 2000, coolant_temp_req_list, 1);

/* Engine Oil Temp */
Request *engine_oil_temp_req_list[] = {&eng_oil_tmp_req};
Info engine_oil_temp = Info("Engine Oil Temp", Unit_Type::Temp, 1000, engine_oil_temp_req_list, 1);

/* Intake Air Temp */
Request *intake_air_temp_req_list[] = {&int_air_tmp_req};
Info intake_air_temp = Info("Intake Air Temp", Unit_Type::Temp, 100, intake_air_temp_req_list, 1);

/* Charge Pressure */
Request *charge_pressure_req_list[] = {&chg_pres_req};
Info charge_pressure = Info("Charge Pressure", Unit_Type::Pressure, 100, charge_pressure_req_list, 1);

/* Intake Pressure */
Request *intake_pressure_req_list[] = {&int_pres_req};
Info intake_pressure = Info("Intake Pressure", Unit_Type::Pressure, 20, intake_pressure_req_list, 1);

/* Ambient Pressure */
Request *ambient_pressure_req_list[] = {&amb_pres_req};
Info ambient_pressure = Info("Ambient Pressure", Unit_Type::Pressure, 1000, ambient_pressure_req_list, 1);

/* Actual Intake Pressure */
Request *actual_intake_pressure_req_list[] = {&amb_pres_req, &int_pres_req};
Info actual_intake_pressure = Info("Intake Pressure Act.", Unit_Type::Pressure, 20, actual_intake_pressure_req_list, 2);

/* Engine RPM */
Request *engine_rpm_req_list[] = {&eng_rpm_req};
Info engine_rpm = Info("Engine Speed", Unit_Type::RPM, 20, engine_rpm_req_list, 1);

/* Engine Torque */
Request *engine_torque_req_list[] = {&eng_trq_req};
Info engine_torque = Info("Engine Torque", Unit_Type::Torque, 20, engine_torque_req_list, 1);

/* Vehicle Speed */
Request *vehicle_speed_req_list[] = {&car_spd_req};
Info vehicle_speed = Info("Vehicle Speed", Unit_Type::Speed, 100, vehicle_speed_req_list, 1);

/* Fuel Level */
Request *fuel_level_req_list[] = {&fuel_lvl_req};
Info fuel_level = Info("Fuel", Unit_Type::Volume, 5000, fuel_level_req_list, 1);

/* Fuel Percent */
Request *fuel_percent_req_list[] = {&fuel_pcnt_req};
Info fuel_percent = Info("Fuel", Unit_Type::Percent, 5000, fuel_percent_req_list, 1);

/* Throttle Position */
Request *throttle_position_req_list[] = {&thrtl_pos_req};
Info throttle_position = Info("Throttle Position", Unit_Type::Percent, 20, throttle_position_req_list, 1);

/* Misfires */
Request *misfire_sum_req_list[] = {&msfr_sum_req};
Info misfire_sum = Info("Misfire Count", Unit_Type::None, 500, misfire_sum_req_list, 1);

/* Cyl 1 Misfires */
Request *misfire_cyl_1_req_list[] = {&msfr_1_req};
Info misfire_cyl_1 = Info("Misfire Count Cyl 1", Unit_Type::None, 500, misfire_cyl_1_req_list, 1);

/* Cyl 2 Misfires */
Request *misfire_cyl_2_req_list[] = {&msfr_2_req};
Info misfire_cyl_2 = Info("Misfire Count Cyl 2", Unit_Type::None, 500, misfire_cyl_2_req_list, 1);

/* Cyl 3 Misfires */
Request *misfire_cyl_3_req_list[] = {&msfr_3_req};
Info misfire_cyl_3 = Info("Misfire Count Cyl 3", Unit_Type::None, 500, misfire_cyl_3_req_list, 1);

/* Cyl 4 Misfires */
Request *misfire_cyl_4_req_list[] = {&msfr_4_req};
Info misfire_cyl_4 = Info("Misfire Count Cyl 4", Unit_Type::None, 500, misfire_cyl_4_req_list, 1);

/* Cyl 1 Timing Correction */
Request *timing_corr_1_req_list[] = {&tim_cor_1_req};
Info timing_corr_1 = Info("Timing Corr. Cyl 1", Unit_Type::Degrees, 100, timing_corr_1_req_list, 1);

/* Cyl 2 Timing Correction */
Request *timing_corr_2_req_list[] = {&tim_cor_2_req};
Info timing_corr_2 = Info("Timing Corr. Cyl 2", Unit_Type::Degrees, 100, timing_corr_2_req_list, 1);

/* Cyl 3 Timing Correction */
Request *timing_corr_3_req_list[] = {&tim_cor_3_req};
Info timing_corr_3 = Info("Timing Corr. Cyl 3", Unit_Type::Degrees, 100, timing_corr_3_req_list, 1);

/* Cyl 4 Timing Correction */
Request *timing_corr_4_req_list[] = {&tim_cor_4_req};
Info timing_corr_4 = Info("Timing Corr. Cyl 4", Unit_Type::Degrees, 100, timing_corr_4_req_list, 1);

Info *info_list[NUM_INFO] = {&coolant_temp, &engine_oil_temp, &intake_air_temp, &charge_pressure,
                             &intake_pressure, &ambient_pressure, &actual_intake_pressure, &engine_rpm,
                             &engine_torque, &vehicle_speed, &fuel_level, &fuel_percent, &throttle_position,
                             &misfire_sum, &misfire_cyl_1, &misfire_cyl_2, &misfire_cyl_3, &misfire_cyl_4,
                             &timing_corr_1, &timing_corr_2, &timing_corr_3, &timing_corr_4};

/**********
 * Panels *
 **********/

Panel panel_1 = Panel(&coolant_temp);
Panel panel_2 = Panel(&throttle_position);
Panel panel_3 = Panel(&intake_air_temp);
Panel panel_4 = Panel(&engine_oil_temp);
Panel panel_5 = Panel(&actual_intake_pressure);

Panel *panel_list[NUM_PANELS] = {&panel_1, &panel_2, &panel_3, &panel_4, &panel_5};

/***********
 * Screens *
 ***********/

Info_Screen screen_1 = Info_Screen(panel_list, 5);
Configurator config = Configurator();

void setup_info_transforms()
{
    for (uint8_t i = 0; i < NUM_INFO; i++)
    {
        info_list[i]->set_transform(transform_default);
    }
}

#endif