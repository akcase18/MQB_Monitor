#include "can_handler.hpp"

/***********
 * Defines *
 ***********/

#define CAN_ID 0x7E0 // CAN ID for UDP requests
#define NO_OFFSET 0  // No CAN data offset
#define NO_SCALE 1   // No CAN data scaling

/*************
 * CAN Setup *
 *************/

FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> CAN; // Initialize the CAN port

/******************
 * Main Functions *
 ******************/

void can_init()
{
    CAN.begin();                 // Start CAN
    CAN.setBaudRate(500000);     // Set baudrate to 500k
    CAN.setMBFilter(REJECT_ALL); // Initialize mailbox to reject all CAN messages
    CAN.setMBFilter(MB1, 0x7E8); // Allow mailbox 1 to accept CAN messages from MQB
    CAN.enableMBInterrupt(MB1);  // Enable interrupts for mailbox 1
    CAN.onReceive(can_recv);     // Run can_recv() when a CAN message is received
}

void can_loop()
{
    CAN.events();
}

void can_send(Information info)
{
    CAN_message_t msg;
    msg.buf[0] = 0x03;
    msg.buf[1] = 0x22;
    msg.buf[2] = info.get_byte_1();
    msg.buf[3] = info.get_byte_2();

    msg.id = CAN_ID;

    CAN.write(msg);
}

void can_recv(const CAN_message_t &msg)
{
    uint8_t byte_1 = msg.buf[2];
    uint8_t byte_2 = msg.buf[3];

    Information info = get_information_panel(byte_1, byte_2);

    int32_t x = 0;

    if (msg.buf[0] == 0x04)
    {
        x = msg.buf[4];
    }
    else
    {
        x = (msg.buf[4] << 8) | msg.buf[5];
    }
    x = (x + info.get_offset()) * info.get_scale();
    info.set_data(x);
}

/********************
 * Helper Functions *
 ********************/

Information get_information_panel(uint8_t byte_1, uint8_t byte_2)
{
    // Loop through potential Information objects (ideally through the panels that are available)
    // Find which one matches the correct bytes
    // Return correct Information object
}

void convert_units(Information info)
{
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
    switch (info.get_unit_type())
    {
    case Information::VOLUME:
        info.set_data(round(info.get_data_num(), 1));
        break;
    case Information::PRESSURE:
        if (info.get_unit_region() == Information::IMPERIAL)
        {
            info.set_data(round(info.get_data_num(), 1));
        }
        else
        {
            info.set_data(round(info.get_data_num(), 2));
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
        info.set_data((int)info.get_data_num());
        break;
    }
}

float round(float value, uint8_t decimal_places)
{
    int round_value = pow(10, decimal_places);

    float val = (int)(value * round_value + 0.5);
    return (float)(val / round_value);
}