#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <FlexCAN_T4.h>
#include <queue>
#include "Configurator.hpp"

using namespace std;

/*************
 * CAN Setup *
 *************/

// static FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> MQB_CAN;
static FlexCAN_T4<CAN3, RX_SIZE_256, TX_SIZE_16> MQB_CAN;

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
 * Sends a UDP request message on the CAN buf for the given Information
 * object.
 */
void can_send(Information *info);

/**
 * Callback function for receiving CAN messages. Automatically called by
 * the FlexCAN callback. Takes in the data, automatically determines which
 * Information object the CAN message was a response to, and sets the data
 * within the Information object.
 * 
 * @param msg the address to a CAN Message object
 */
void can_recv(const CAN_message_t &msg);

/********************
 * Helper Functions *
 ********************/

/**
 * Determines what Information object the data received is coming from 
 * based on the received PIDs.
 * 
 * @param byte_1 the first byte of the PID
 * @param byte_2 the second byte of the PID
 * 
 * @return the Information object that the data was received from
 */
Information *get_info_panel(uint8_t byte_1, uint8_t byte_2);

/**
 * Conters the data in an Information object from metric to imperial units
 * using the type of data it is.
 * 
 * @param info the Information object that holds the data to convert
 */
void convert_units(Information *info, float data);

/**
 * Automatically rounds the data in an Information object based on the type
 * of data it is.
 * 
 * @param info the Information object that holds the data to round
 */
void round_data(Information *info);

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