#include "Arduino.h"

#define NO_OFFSET 0
#define NO_SCALE 1

struct Request
{
    uint16_t byte_1; // First byte of UDS request code (in hex)
    uint16_t byte_2; // Second byte of UDS request code (in hex)
    float offset;    // Offset value to decode raw data
    float scale;     // Scaling factor to decode raw data
    float data = 0;  // Received data (decoded)
};

/***************************
 * CAN Request Declaration *
 ***************************/

/***** Performance Metrics *****/
Request cool_tmp_req = { // Coolant Temperature
    .byte_1 = 0xF4,
    .byte_2 = 0x05,
    .offset = -40,
    .scale = NO_SCALE};
Request eng_oil_tmp_req = { // Engine Oil Temperature
    .byte_1 = 0x20,
    .byte_2 = 0x2F,
    .offset = -2731.4,
    .scale = 0.1};
Request int_air_tmp_req = { // Intake Air Temperature
    .byte_1 = 0xF4,
    .byte_2 = 0x0F,
    .offset = -40,
    .scale = NO_SCALE};
Request chg_pres_req = { // Charge Pressure
    .byte_1 = 0x20,
    .byte_2 = 0x2a,
    .offset = NO_OFFSET,
    .scale = 0.001};
Request int_pres_req = { // Intake Pressure
    .byte_1 = 0x3E,
    .byte_2 = 0x70,
    .offset = NO_OFFSET,
    .scale = 0.001};
Request amb_pres_req = { // Ambient Pressure
    .byte_1 = 0x13,
    .byte_2 = 0xCA,
    .offset = NO_OFFSET,
    .scale = 0.0001};
Request lmd_act_req = { // Actual Lambda
    .byte_1 = 0x10,
    .byte_2 = 0xC0,
    .offset = NO_OFFSET,
    .scale = (1 / 1024)};
Request lmd_spec_req = { // Specified Lambda
    .byte_1 = 0x29,
    .byte_2 = 0x50,
    .offset = NO_OFFSET,
    .scale = (1 / 1024)};
Request trbo_spd_req = { // Turbo Speed
    .byte_1 = 0x10,
    .byte_2 = 0x40,
    .offset = NO_OFFSET,
    .scale = 6.11};

/***** Car Information *****/
Request eng_rpm_req = { // Engine RPM
    .byte_1 = 0x20,
    .byte_2 = 0x6E,
    .offset = NO_OFFSET,
    .scale = NO_SCALE};
Request eng_trq_req = { // Engine Torque
    .byte_1 = 0x43,
    .byte_2 = 0x7C,
    .offset = NO_OFFSET,
    .scale = 0.1};
Request car_spd_req = { // Vehicle Speed
    .byte_1 = 0x20,
    .byte_2 = 0x33,
    .offset = NO_OFFSET,
    .scale = 0.01};
Request fuel_lvl_req = { // Fuel (Volume)
    .byte_1 = 0x22,
    .byte_2 = 0x06,
    .offset = NO_OFFSET,
    .scale = NO_SCALE};
Request fuel_pcnt_req = { // Fuel (Percentage)
    .byte_1 = 0xF4,
    .byte_2 = 0x2F,
    .offset = NO_OFFSET,
    .scale = (1 / 2.55)};
Request thrtl_pos_req = { // Throttle Position
    .byte_1 = 0xF4,
    .byte_2 = 0x11,
    .offset = NO_OFFSET,
    .scale = (1 / 2.55)};

/***** Diag/Monitoring *****/
Request msfr_sum_req = { // Misfires (Sum)
    .byte_1 = 0x29,
    .byte_2 = 0x04,
    .offset = NO_OFFSET,
    .scale = NO_SCALE};
Request msfr_1_req = { // Misfires (Cylinder 1)
    .byte_1 = 0x29,
    .byte_2 = 0x66,
    .offset = NO_OFFSET,
    .scale = NO_SCALE};
Request msfr_2_req = { // Misfires (Cylinder 2)
    .byte_1 = 0x29,
    .byte_2 = 0x67,
    .offset = NO_OFFSET,
    .scale = NO_SCALE};
Request msfr_3_req = { // Misfires (Cylinder 3)
    .byte_1 = 0x29,
    .byte_2 = 0x68,
    .offset = NO_OFFSET,
    .scale = NO_SCALE};
Request msfr_4_req = { // Misfires (Cylinder 4)
    .byte_1 = 0x29,
    .byte_2 = 0x69,
    .offset = NO_OFFSET,
    .scale = NO_SCALE};
Request tim_cor_1_req = { // Timing Correction (Cylinder 1)
    .byte_1 = 0x20,
    .byte_2 = 0x0A,
    .offset = NO_OFFSET,
    .scale = 0.01};
Request tim_cor_2_req = { // Timing Correction (Cylinder 2)
    .byte_1 = 0x20,
    .byte_2 = 0x0B,
    .offset = NO_OFFSET,
    .scale = 0.01};
Request tim_cor_3_req = { // Timing Correction (Cylinder 3)
    .byte_1 = 0x20,
    .byte_2 = 0x0C,
    .offset = NO_OFFSET,
    .scale = 0.01};
Request tim_cor_4_req = { // Timing Correction (Cylinder 4)
    .byte_1 = 0x20,
    .byte_2 = 0x0D,
    .offset = NO_OFFSET,
    .scale = 0.01};