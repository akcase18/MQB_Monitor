#include "Arduino.h"

#define NO_OFFSET 0
#define NO_SCALE  1

struct Request
{
    uint16_t byte_1; // First byte of UDS request code (in hex)
    uint16_t byte_2; // Second byte of UDS request code (in hex)
    int16_t offset;  // Offset value to decode raw data
    float scale;     // Scaling factor to decode raw data
    float data;      // Received data (decoded)
};

/***************************
 * CAN Request Declaration *
 ***************************/

/***** Performance Metrics *****/
extern Request cool_tmp_req;    // Coolant Temperature
extern Request eng_oil_tmp_req; // Engine Oil Temperature
extern Request int_air_tmp_req; // Intake Air Temperature
extern Request chg_pres_req;    // Charge Pressure
extern Request int_pres_req;    // Intake Pressure
extern Request amb_pres_req;    // Ambient Pressure
extern Request lda_act_req;     // Actual Lambda
extern Request lda_spec_req;    // Specified Lambda
extern Request trbo_spd_req;    // Turbo Speed

/***** Car Information *****/
extern Request eng_rpm_req;   // Engine RPM
extern Request eng_trq_req;   // Engine Torque
extern Request car_spd_req;   // Vehicle Speed
extern Request fuel_lvl_req;  // Fuel (Volume)
extern Request fuel_pcnt_req; // Fuel (Percentage)
extern Request thrtl_pos_req; // Throttle Position

/***** Diag/Monitoring *****/
extern Request msfr_sum_req;  // Misfires (Sum)
extern Request msfr_1_req;    // Misfires (Cylinder 1)
extern Request msfr_2_req;    // Misfires (Cylinder 2)
extern Request msfr_3_req;    // Misfires (Cylinder 3)
extern Request msfr_4_req;    // Misfires (Cylinder 4)
extern Request tim_cor_1_req; // Timing Correction (Cylinder 1)
extern Request tim_cor_2_req; // Timing Correction (Cylinder 2)
extern Request tim_cor_3_req; // Timing Correction (Cylinder 3)
extern Request tim_cor_4_req; // Timing Correction (Cylinder 4)