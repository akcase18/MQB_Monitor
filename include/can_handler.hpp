#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <FlexCAN_T4.h>
#include "Request.hpp"

using namespace std;

/*************
 * CAN Setup *
 *************/

// static FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> MQB_CAN;
static FlexCAN_T4<CAN3, RX_SIZE_16, TX_SIZE_16> MQB_CAN;

/******************
 * Main Functions *
 ******************/

/**
 * Initializes the CAN bus and sets baudrate, hardware filters, callback
 * functions, and interrups.
 */
void can_init();

/**
 * Function called in the main loop to handle all callback functions and
 * interrupts automatically.
 */
void can_loop();

/**
 * Sends a UDS request message on the CAN buf for the given Information
 * object.
 */
void can_send();

/**
 * Callback function for receiving CAN messages. Automatically called by
 * the FlexCAN callback. Takes in the data, determines which Request the
 * message was a response to, and sets the data accordingly.
 * 
 * @param msg the address to a CAN Message object
 */
void can_recv(const CAN_message_t &msg);

/********************
 * Helper Functions *
 ********************/

/**
 * Converts data from metric to imperial
 */
void convert_units();

/**
 * Rounds data based on it's data type
 */
void round_data();

/**
 * Rounding helper function to round a given value to a certain number
 * of decimal places.
 * 
 * @param value the value to round
 * @param decimal_places the number of decimal places to round the given value to
 * 
 * @return the rounded value with the correct number of decimal places
 */
float round(float value, uint8_t decimal_places);

#endif