#ifdef USE_ARDUINO

    #include "can_handler.hpp"

    /*==============================================================================
     Defines
     =============================================================================*/

    /* CAN ID for UDP requests */
    #define CAN_ID            0x7E0
    #define NUM_DATA_BYTES    0x03
    #define ENHANCE_DATA_REQ  0x22
    #define ENHANCE_DATA_RESP 0x62

/*==============================================================================
 Main Functions
 =============================================================================*/

void can_init()
{
    MQB_CAN.begin();             // Start CAN
    MQB_CAN.setBaudRate(500000); // Set baudrate to 500k
    MQB_CAN.setMBFilter(
        REJECT_ALL); // Initialize mailbox to reject all CAN messages
    MQB_CAN.setMBFilter(
        MB1, 0x7E8); // Allow mailbox 1 to accept CAN messages from MQB
    MQB_CAN.enableMBInterrupt(MB1); // Enable interrupts for mailbox 1
    MQB_CAN.onReceive(
        can_recv); // Setup callback function for receiving CAN message
}

void can_loop()
{
    MQB_CAN.events();
}

void can_send(Request *req)
{
    CAN_message_t msg; // Initialize the CAN message to send to the car
    msg.buf[0] = NUM_DATA_BYTES; // Tell the car you are sending 3 more bytes in
                                 // this request
    msg.buf[1] = ENHANCE_DATA_REQ; // Request enhanced service data from the car
    msg.buf[2] = req->byte_1;      // UDS request byte 1
    msg.buf[3] = req->byte_2;      // UDS request byte 2
    msg.buf[4] = 0;
    msg.buf[5] = 0;
    msg.buf[6] = 0;
    msg.buf[7] = 0;
    msg.len    = 8;

    msg.id = CAN_ID;

    MQB_CAN.write(msg);
}

void can_recv(const CAN_message_t &msg)
{
    uint8_t byte_1
        = msg.buf[2]; // Get the first byte that the message is responding to
    uint8_t byte_2
        = msg.buf[3]; // Get the second byte that the message is responding to

    /* Check that CAN message is a valid response */
    if (msg.buf[1] != ENHANCE_DATA_RESP)
    {
        return;
    }

    // Figure out which Request the message was a response to
    Request *req = get_request(byte_1, byte_2);

    int16_t raw_data = 0; // This is where data will go

    if (msg.buf[0] == 0x04) // Data comes in 1 byte
    {
        raw_data = msg.buf[4];
    }
    else // Data comes in 2 bytes
    {
        raw_data = (msg.buf[4] << 8) | msg.buf[5]; // Concatenate data bytes
    }
    float new_data = raw_data;
    // Apply offset and scale
    new_data = (new_data + req->offset) * req->scale;
    req->set_data(new_data);
}

/*==============================================================================
 Helper Functions
 =============================================================================*/

Request *get_request(uint16_t byte_1, uint16_t byte_2)
{
    for (uint8_t i = 0; i < NUM_REQS; i++)
    {
        if ((request_list[i]->byte_1 == byte_1)
            && (request_list[i]->byte_2 == byte_2))
        {
            return request_list[i];
        }
    }
    return nullptr;
}

#endif // USE_ARDUINO