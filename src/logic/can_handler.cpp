#include "can_handler.hpp"

/***********
 * Defines *
 ***********/

/* CAN ID for UDP requests */
#define CAN_ID 0x7E0
#define NUM_DATA_BYTES 0x03
#define ENHANCE_DATA_REQ 0x22
#define ENHANCE_DATA_RESP 0x62

/******************
 * Main Functions *
 ******************/

void can_init()
{
    MQB_CAN.begin();                 // Start CAN
    MQB_CAN.setBaudRate(500000);     // Set baudrate to 500k
    MQB_CAN.setMBFilter(REJECT_ALL); // Initialize mailbox to reject all CAN messages
    MQB_CAN.setMBFilter(MB1, 0x7E8); // Allow mailbox 1 to accept CAN messages from MQB
    MQB_CAN.enableMBInterrupt(MB1);  // Enable interrupts for mailbox 1
    MQB_CAN.onReceive(can_recv);     // Setup callback function for receiving CAN message
}

void can_loop()
{
    MQB_CAN.events();
}

void can_send()
{
    CAN_message_t msg;             // Initialize the CAN message to send to the car
    msg.buf[0] = NUM_DATA_BYTES;   // Tell the car you are sending 3 more bytes in this request
    msg.buf[1] = ENHANCE_DATA_REQ; // Request enhanced service data from the car
    msg.buf[2] = 0xFF;             // UDS request byte 1
    msg.buf[3] = 0xFF;             // UDS request byte 2
    msg.buf[4] = 0;
    msg.buf[5] = 0;
    msg.buf[6] = 0;
    msg.buf[7] = 0;
    msg.len = 8;

    msg.id = CAN_ID;

    MQB_CAN.write(msg);
}

void can_recv(const CAN_message_t &msg)
{
    uint8_t byte_1 = msg.buf[2]; // Get the first byte that the message is responding to
    uint8_t byte_2 = msg.buf[3]; // Get the second byte that the message is responding to

    /* Check that CAN message is a valid response */
    if (msg.buf[1] != ENHANCE_DATA_RESP)
    {
        return;
    }

    // Figure out which Request the message was a response to

    float x = 0; // This is where data will go

    if (msg.buf[0] == 0x04) // Data comes in 1 byte
    {
        x = msg.buf[4];
    }
    else // Data comes in 2 bytes
    {
        x = (msg.buf[4] << 8) | msg.buf[5]; // Concatenate data together
    }
    // Apply offset and scale
    // Convert to imperial if necessary
}

/********************
 * Helper Functions *
 ********************/

void convert_units()
{
}

void round_data()
{
}

float round(float value, uint8_t decimal_places)
{
    /* I don't remember how this works, but it's online somewhere */
    int round_value = pow(10, decimal_places);

    float val = (int)(value * round_value + 0.5);
    return (float)(val / round_value);
}