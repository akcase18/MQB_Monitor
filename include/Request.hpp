#include "Arduino.h"

using namespace std;

/***********
 * Defines *
 ***********/

#define NO_OFFSET 0
#define NO_SCALE 1

class Request
{
public:
    /********************
     * Public Variables *
     ********************/

    uint16_t byte_1; // First byte of UDS request code (in hex)
    uint16_t byte_2; // Second byte of UDS request code (in hex)
    int16_t offset;  // Offset value to decode raw data
    float scale;     // Scaling factor to decode raw data
    float data;      // Received data (decoded)

    /********************
     * Public Functions *
     ********************/
    
    /**
     * Constructor to pass an existing Request object into a new one.
     *
     * This will copy all parameters and variables from the given Request
     * object and assign them to the new Request object.
     *
     * @param request A pointer to an existing Request object.
     */
    Request(Request *request);

    /**
     * Constructor to fully define a new Request object.
     *
     * @param new_byte_1 The first byte of the UDS request code (in hex).
     * @param new_byte_2 The second byte of the UDS request code (in hex).
     * @param new_offset The value by which the incoming raw data should be shifted by to decode.
     * @param new_scale The scaling factor for the incoming raw data.
     */
    Request(uint16_t new_byte_1, uint16_t new_byte_2, int16_t new_offset, float new_scale);
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