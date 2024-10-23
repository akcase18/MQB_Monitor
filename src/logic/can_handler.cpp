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

void can_send(Information *info)
{
    CAN_message_t msg;               // Initialize the CAN message to send to the car
    msg.buf[0] = NUM_DATA_BYTES;     // Tell the car you are sending 3 more bytes in this request
    msg.buf[1] = ENHANCE_DATA_REQ;   // Request enhanced service data from the car
    msg.buf[2] = info->get_byte_1(); // UDS request byte 1
    msg.buf[3] = info->get_byte_2(); // UDS request byte 2
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

    if (msg.buf[1] != ENHANCE_DATA_RESP)
    {
        return;
    }

    Information *info = get_info_panel(byte_1, byte_2);

    float x = 0; // This is where data will go

    if (msg.buf[0] == 0x04) // Data comes in 1 byte
    {
        x = msg.buf[4];
    }
    else // Data comes in 2 bytes
    {
        x = (msg.buf[4] << 8) | msg.buf[5]; // Concatenate data together
    }
    x = (x + info->get_offset()) * info->get_scale(); // Apply offset and scale
    if (info->get_is_metric())
    {
        info->set_data(x);
    }
    else
    {
        convert_units(info, x);
    }
}

/********************
 * Helper Functions *
 ********************/

Information *get_info_panel(uint8_t byte_1, uint8_t byte_2)
{
    /* Loop through potential Information objects */
    for (uint8_t i = 0; i < info_list.size(); i++)
    {
        Information *info = info_list[i];                                     // Get the current Information object
        if ((byte_1 == info->get_byte_1()) && (byte_2 == info->get_byte_2())) // Check if both UDS request bytes match
        {
            return info; // Return the Information object if both matches are found
        }
    }
    return nullptr; // Return null if no matches were found *extraneous data*
}

void convert_units(Information *info, float data)
{
    /* Check what type of data the Information object has */
    float value = 0;
    switch (info->get_unit_type())
    {
    case Information::PRESSURE:
        value = data * 14.5038;
        info->set_data(value);
        break;
    case Information::SPEED:
        value = data / 1.609;
        info->set_data(value);
        break;
    case Information::TEMP:
        value = data * 1.8 + 32;
        info->set_data(value);
        break;
    case Information::TORQUE:
        value = data * 0.7375621493;
        info->set_data(value);
        break;
    case Information::VOLUME:
        value = data / 3.785;
        info->set_data(value);
        break;
    default:
        info->set_data(data);
        break;
    }
}

void round_data(Information *info)
{
    /* Check what type of data the Information object has */
    switch (info->get_unit_type())
    {
    case Information::VOLUME:
        info->set_data(round(info->get_data_num(), 1)); // 1 decimal place for volume
        break;
    case Information::PRESSURE:
        if (info->get_is_metric())
        {
            info->set_data(round(info->get_data_num(), 2)); // 2 decimal places for Metric
        }
        else
        {
            info->set_data(round(info->get_data_num(), 1)); // 1 decimal place for Imperial
        }
        break;
    default:
        info->set_data((int)info->get_data_num()); // No decimal places for any other Information type
        break;
    }
}

float round(float value, uint8_t decimal_places)
{
    /* I don't remember how this works, but it's online somewhere */
    int round_value = pow(10, decimal_places);

    float val = (int)(value * round_value + 0.5);
    return (float)(val / round_value);
}