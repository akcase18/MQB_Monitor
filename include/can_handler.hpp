#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <FlexCAN_T4.h>
#include "Request.hpp"

using namespace std;

/*************
 * CAN Setup *
 *************/

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
void can_send(Request *req);

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
 * Get the Request object the message was a response to.
 */
Request *get_request(uint16_t byte_1, uint16_t byte_2);

#endif