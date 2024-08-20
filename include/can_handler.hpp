#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <FlexCAN_T4.h>
#include "Configurator.hpp"

/*************
 * CAN Setup *
 *************/

static FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> MQB_CAN; // Initialize the CAN port

/******************
 * Main Functions *
 ******************/

/**
 * @brief Initializes the CAN bus and sets baudrate, hardware filters, callback
 * functions, and interrupts.
 */
void can_init();

/**
 * @brief Function called in the main loop to handle all callback functions and
 * interrupts automatically.
 */
void can_loop();

/**
 * @brief Sends a UDP request message on the CAN bus for the given Information
 * object.
 *
 * @param info The Information object to request data for
 */
void can_send(Information info);

/**
 * @brief Callback function for receiving CAN messages. Automatically called by
 * the FlexCAN callback. Takes in the data, automcatically determines which
 * Information object the CAN message was a response to, and sets the data within
 * the Information object.
 *
 * @param msg The address to a CAN Message object
 */
void can_recv(const CAN_message_t &msg);

/********************
 * Helper Functions *
 ********************/

/**
 * @brief Determines what information the data received is coming from based on the
 * received PIDs.
 *
 * @param byte_1 The first byte of the PID
 * @param byte_2 The second byte of the PID
 */
Information get_information_panel(uint8_t byte_1, uint8_t byte_2);

/**
 * @brief Converts the data in an Information object from metric to imperial units
 * using the type of data it is.
 *
 * @param info The Information object that holds the data to convert
 */
void convert_units(Information info);

/**
 * @brief Automatically rounds the data in an Information object based on the type
 * of data it is.
 *
 * @param info The Information object that holds the data to round
 */
void round_data(Information info);

/**
 * @brief Rounding helper function to round a given value to a certain number of
 * decimal places.
 *
 * @param value The value to round
 * @param decimal_places The number of decimal places to round the given value to
 *
 * @return The rounded value with the correct number of decimal places
 */
float round(float value, uint8_t decimal_places);

#endif