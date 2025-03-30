#ifndef SETUP_HPP
#define SETUP_HPP

#include "etl/unordered_map.h"
#include "Request.hpp"

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

#endif