#include "Request.hpp"

/**************************
 * CAN Request Definition *
 **************************/

/***** Performance Metrics *****/
Request cool_tmp_req = Request(0xF4, 0x05, -40, NO_SCALE);
Request eng_oil_tmp_req = Request(0x20, 0x2F, -2731.4, 0x1);
Request int_air_tmp_req = Request(0xF4, 0x0F, -40, NO_SCALE);
Request chg_pres_req = Request(0x20, 0x2A, NO_OFFSET, 0.001);
Request int_pres_req = Request(0x3E, 0x70, NO_OFFSET, 0.001);
Request amb_pres_req = Request(0x13, 0xCA, NO_OFFSET, 0.0001);
Request lda_act_req = Request(0x10, 0xC0, NO_OFFSET, (1 / 1024));
Request lda_spec_req = Request(0x29, 0x50, NO_OFFSET, (1 / 1024));
Request trbo_spd_req = Request(0x10, 0x40, NO_OFFSET, 6.11);

/***** Car Information *****/
Request eng_rpm_req = Request(0x20, 0x6E, NO_OFFSET, NO_SCALE);
Request eng_trq_req = Request(0x42, 0x7C, NO_OFFSET, 0.1);
Request car_spd_req = Request(0x20, 0x44, NO_OFFSET, 0.01);
Request fuel_lvl_req = Request(0x22, 0x06, NO_OFFSET, NO_SCALE);
Request fuel_pcnt_req = Request(0xF4, 0x2F, NO_OFFSET, (1 / 2.55));
Request thrtl_pos_req = Request(0xF4, 0x11, NO_OFFSET, (1 / 2.55));

/***** Diag/Monitoring *****/
Request msfr_sum_req = Request(0x29, 0x04, NO_OFFSET, NO_SCALE);
Request msfr_1_req = Request(0x29, 0x66, NO_OFFSET, NO_SCALE);
Request msfr_2_req = Request(0x29, 0x67, NO_OFFSET, NO_SCALE);
Request msfr_3_req = Request(0x29, 0x68, NO_OFFSET, NO_SCALE);
Request msfr_4_req = Request(0x29, 0x69, NO_OFFSET, NO_SCALE);
Request tim_cor_1_req = Request(0x20, 0x0A, NO_OFFSET, 0.01);
Request tim_cor_2_req = Request(0x20, 0x0B, NO_OFFSET, 0.01);
Request tim_cor_3_req = Request(0x20, 0x0C, NO_OFFSET, 0.01);
Request tim_cor_4_req = Request(0x20, 0x0D, NO_OFFSET, 0.01);

/****************
 * Constructors *
 ****************/

Request::Request(Request *request)
{
    byte_1 = request->byte_1;
    byte_2 = request->byte_2;
    offset = request->offset;
    scale = request->scale;
    data = request->data;
}

Request::Request(uint16_t new_byte_1, uint16_t new_byte_2, int16_t new_offset, float new_scale)
{
    byte_1 = new_byte_1;
    byte_2 = new_byte_2;
    offset = new_offset;
    scale = new_scale;
    data = 0.0;
}