#ifndef SETUP_HPP
#define SETUP_HPP

#include "etl/unordered_map.h"
#include "Request.hpp"
#include "Information.hpp"

/*******************
 * Create Requests *
 *******************/

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

etl::unordered_map<uint32_t, Request *, 24> requests;

/**********************************
 * Information Object Declaration *
 **********************************/

/***** Performance Metrics *****/

/* Coolant Temperature */
Request *coolant_temp_req[1] = {&cool_tmp_req};
Information coolant_temp = Information(coolant_temp_req, 1, "Coolant Temp", Information::TEMP, 2000); // Works;

/* Engine Oil Temperature */
Request *engine_oil_temp_req[1] = {&eng_oil_tmp_req};
Information engine_oil_temp = Information(engine_oil_temp_req, 1, "Engine Oil Temp", Information::TEMP, 1000);

/* Intake Air Temperature */
Request *intake_air_temp_req[1] = {&int_air_tmp_req};
Information intake_air_temp = Information(intake_air_temp_req, 1, "Intake Air Temp", Information::TEMP, 100);

/* Charge Pipe Pressure */
Request *charge_pressure_req[1] = {&chg_pres_req};
Information charge_pressure = Information(charge_pressure_req, 1, "Charge Pressure", Information::PRESSURE, 100);

/* Intake Pressure */
Request *intake_pressure_req[1] = {&int_pres_req};
Information intake_pressure = Information(intake_pressure_req, 1, "Intake Pressure", Information::PRESSURE, MAX_REFRESH);

/* Ambient Pressure */
Request *ambient_pressure_req[1] = {&amb_pres_req};
Information ambient_pressure = Information(ambient_pressure_req, 1, "Ambient Pressure", Information::PRESSURE, 1000);

/* Actual Intake Pressure */
Request *actual_intake_pressure[2] = {&int_pres_req, &amb_pres_req};
Information actual_intake_pressure = Information(actual_intake_pressure, 2, "Intake Pressure (Act)", Information::PRESSURE, MAX_REFRESH);

/* Actual Lambda Value */
Request *lambda_actual_req[1] = {&lmd_act_req};
Information lambda_actual = Information(lambda_actual_req, 1, "Lambda (Act.)", Information::LAMBDA, MAX_REFRESH); // Not working

/* Specified Lambda Value */
Request *lambda_specified_req[1] = {&lmd_spec_req};
Information lambda_specified = Information(lambda_specified_req, 1, "Lambda (Spec.)", Information::LAMBDA, MAX_REFRESH); // Not working

/* Turbo Speed */
Request *turbo_speed_req[1] = {&trbo_spd_req};
Information turbo_speed = Information(turbo_speed_req, 1, "Turbo Speed", Information::RPM, MAX_REFRESH);

/***** Car Information *****/

/* Engine RPM */
Request *engine_rpm_req[1] = {&eng_rpm_req};
Information engine_rpm = Information(engine_rpm_req, 1, "Engine RPM", Information::RPM, MAX_REFRESH);

/* Engine Torque */
Request *engine_torque_req[1] = {&eng_trq_req};
Information engine_torque = Information(engine_torque_req, 1, "Engine Torque", Information::TORQUE, MAX_REFRESH);

/* Vehicle Speed */
Request *vehicle_speed_req[1] = {&car_spd_req};
Information vehicle_speed = Information(vehicle_speed_req, 1, "Vehicle Speed", Information::SPEED, 100);

/* Fuel (Level) */
Request *fuel_level_req[1] = {&fuel_lvl_req};
Information fuel_level = Information(fuel_level_req, 1, "Fuel", Information::VOLUME, MIN_REFRESH);

/* Fuel (Percent) */
Request *fuel_percent_req[1] = {&fuel_pcnt_req};
Information fuel_percent = Information(fuel_percent_req, 1, "Fuel", Information::PERCENT, MIN_REFRESH);

/* Throttle Position */
Request *throttle_position_req[1] = {&thrtl_pos_req};
Information throttle_position = Information(throttle_position_req, 1, "Throttle Position", Information::PERCENT, MAX_REFRESH);

/***** Diag/Monitoring *****/

/* Total Number of Misfires */
Request *misfire_sum_req[1] = {&msfr_sum_req};
Information misfire_sum = Information(misfire_sum_req, 1, "Misfire Count", Information::SUM, 500);

/* Total Number of Misfires on Cylinder 1 */
Request *misfire_cyl_1_req[1] = {&msfr_1_req};
Information misfire_cyl_1 = Information(misfire_cyl_1_req, 1, "Misfire Count Cyl 1", Information::SUM, 500);

/* Total Number of Misfires on Cylinder 2 */
Request *misfire_cyl_2_req[1] = {&msfr_2_req};
Information misfire_cyl_2 = Information(misfire_cyl_2_req, 1, "Misfire Count Cyl 2", Information::SUM, 500);

/* Total Number of Misfires on Cylinder 3 */
Request *misfire_cyl_3_req[1] = {&msfr_3_req};
Information misfire_cyl_3 = Information(misfire_cyl_3_req, 1, "Misfire Count Cyl 3", Information::SUM, 500);

/* Total Number of Misfires on Cylinder 4 */
Request *misfire_cyl_4_req[1] = {&msfr_4_req};
Information misfire_cyl_4 = Information(misfire_cyl_4_req, 1, "Misfire Count Cyl 4", Information::SUM, 500);

/* Timing Correction on Cylinder 1 */
Request *timing_corr_1_req[1] = {&tim_cor_1_req};
Information timing_corr_1 = Information(timing_corr_1_req, 1, "Timing Corr. Cyl 1", Information::DEGREES, 100);

/* Timing Correction on Cylinder 2 */
Request *timing_corr_2_req[1] = {&tim_cor_2_req};
Information timing_corr_2 = Information(timing_corr_2_req, 1, "Timing Corr. Cyl 2", Information::DEGREES, 100);

/* Timing Correction on Cylinder 3 */
Request *timing_corr_3_req[1] = {&tim_cor_3_req};
Information timing_corr_3 = Information(timing_corr_3_req, 1, "Timing Corr. Cyl 3", Information::DEGREES, 100);

/* Timing Correction on Cylinder 4 */
Request *timing_corr_4_req[1] = {&tim_cor_4_req};
Information timing_corr_4 = Information(timing_corr_4_req, 1, "Timing Corr. Cyl 4", Information::DEGREES, 100);

/***** Information Object List *****/
Information *info_list[25] = {&coolant_temp, &engine_oil_temp, &intake_air_temp, &charge_pressure,
                              &intake_pressure, &ambient_pressure, &actual_intake_pressure, &lambda_actual,
                              &lambda_specified, &turbo_speed, &engine_rpm, &engine_torque, &vehicle_speed,
                              &fuel_level, &fuel_percent, &throttle_position, &misfire_sum, &misfire_cyl_1,
                              &misfire_cyl_2, &misfire_cyl_3, &misfire_cyl_4, &timing_corr_1, &timing_corr_2,
                              &timing_corr_3, &timing_corr_4};

#endif