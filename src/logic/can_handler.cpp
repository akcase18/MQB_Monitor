#include "can_handler.hpp"

/***********
 * Defines *
 ***********/

#define CAN_ID 0x7E0 // CAN ID for UDP requests
#define NO_OFFSET 0  // No CAN data offset
#define NO_SCALE 1   // No CAN data scaling

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
    MQB_CAN.events(); // Checks for event updates
}

void can_send(Information info)
{
    CAN_message_t msg; // Initialize the CAN message
    msg.buf[0] = 0x03; // Represents 3 more bytes of data in the data request
    msg.buf[1] = 0x22; // Enhanced data request code
    msg.buf[2] = info.get_byte_1(); // UDS request byte 1
    msg.buf[3] = info.get_byte_2(); // UDS request byte 2

    msg.id = CAN_ID; // Set the CAN ID of the message

    MQB_CAN.write(msg); // Write CAN message to CAN bus
}

void can_recv(const CAN_message_t &msg)
{
    uint8_t byte_1 = msg.buf[2]; // Get the first UDS request byte that the message is responding to
    uint8_t byte_2 = msg.buf[3]; // Get the second UDS request byte that the message is responding to

    Information info = get_information_panel(byte_1, byte_2);

    int32_t x = 0; // Initialize where data will go

    if (msg.buf[0] == 0x04) // Check whether data comes in 1 byte
    {
        x = msg.buf[4];
    }
    else // Data comes in 2 bytes
    {
        x = (msg.buf[4] << 8) | msg.buf[5]; // Concatenate data together
    }
    x = (x + info.get_offset()) * info.get_scale(); // Apply offset and scale
    info.set_data(x); // Put data into Information object
}

/********************
 * Helper Functions *
 ********************/

Information get_information_panel(uint8_t byte_1, uint8_t byte_2)
{
    /* Loop through potential Information objects */
    for (uint i = 0; i < Layout::info_list.size(); i++)
    {
        Information info = Layout::info_list[i]; // Get current Information object
        if (byte_1 == info.get_byte_1())         // Check if first byte of UDS request matches
        {
            if (byte_2 == info.get_byte_2()) // Check if second byte of UDS request matches
            {
                return info; // Return if both bytes of UDS request match
            }
        }
    }
    return nullptr; // Return NULL if no matches are found
}

void convert_units(Information info)
{
    /* Check what type of data the Information object has */
    switch (info.get_unit_type())
    {
    case Information::PRESSURE:
        info.set_data((info.get_data_num() * 14.5038));
        break;
    case Information::SPEED:
        info.set_data((info.get_data_num() / 1.609));
        break;
    case Information::TEMP:
        info.set_data((info.get_data_num() * (9 / 5)) + 32);
        break;
    case Information::TORQUE:
        info.set_data((info.get_data_num() * 0.7375621493));
        break;
    case Information::VOLUME:
        info.set_data((info.get_data_num() / 3.785));
        break;
    default:
        break;
    }
}

void round_data(Information info)
{
    /* Check what type of data the Information object has */
    switch (info.get_unit_type())
    {
    case Information::VOLUME:
        info.set_data(round(info.get_data_num(), 1)); // Only want 1 decimal place for volume
        break;
    case Information::PRESSURE:
        if (info.get_unit_region() == Information::IMPERIAL)
        {
            info.set_data(round(info.get_data_num(), 1)); // 1 decimal place for Imperial pressure
        }
        else
        {
            info.set_data(round(info.get_data_num(), 2)); // 2 decimal places for Metric pressure
        }
        break;
    case Information::TEMP:
    case Information::SPEED:
    case Information::PERCENT:
    case Information::RPM:
    case Information::TORQUE:
    case Information::DEGREES:
    case Information::NONE:
    default:
        info.set_data((int)info.get_data_num()); // No decimal places for everything else
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