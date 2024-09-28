#ifndef ISO_TP_HPP
#define ISO_TP_HPP

#include <FlexCAN_T4.h>
#include <isotp.h>
#include "Configurator.hpp"

/**********************
 * CAN + ISO-TP Setup *
 **********************/

isotp<RX_BANKS_16, 512> tp;
DMAMEM static FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> can0;

/******************
 * Main Functions *
 ******************/

void tp_init();

void tp_loop();

void tp_send(Information *info);

void tp_recv(const ISOTP_data &config, const uint8_t *buf);

/********************
 * Helper Functions *
 ********************/

Information *get_info_panel_tp(uint8_t byte_1, uint8_t byte_2);

void convert_units_tp(Information *info);

void round_data_tp(Information *info);

float round_tp(float value, uint8_t decimal_places);

#endif