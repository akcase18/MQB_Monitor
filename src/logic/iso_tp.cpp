#include "iso_tp.hpp"

/******************
 * Main Functions *
 ******************/

void tp_init()
{
    can0.begin();
    can0.setBaudRate(500000);
    can0.setMBFilter(REJECT_ALL);
    can0.setMBFilter(MB1, 0x7E8);
    can0.enableFIFO();
    can0.enableFIFOInterrupt();
    tp.begin();
    tp.setWriteBus(&can0);
    tp.onReceive(tp_recv);
}

void tp_loop()
{
    // Not needed I guess
}

void tp_send(vector<Information *> infos)
{
    ISOTP_data config;
    config.id = 0x7E0;
    config.flags.extended = 0;
    config.separation_time = 10;
    uint8_t buf[infos.size() * 4]; // 4 bytes per Information object request
    /* Loop through all Information objects to send */
    for (uint8_t i = 0; i < infos.size(); i++)
    {
        uint8_t scaled_i = i * 4;                   // 4 bytes per object, so this lets us index properly
        buf[scaled_i] = 0x03;                       // Number of bytes you are sending in the request
        buf[scaled_i + 1] = 0x22;                   // Enhanced service data
        buf[scaled_i + 2] = infos[i]->get_byte_1(); // Byte 1 of UDS request
        buf[scaled_i + 3] = infos[i]->get_byte_2(); // Byte 2 of UDS request
    }
    tp.write(config, buf, sizeof(buf));
}

void tp_recv(const ISOTP_data &config, const uint8_t *buf)
{
    /* Loop through all received data */
    for (uint8_t i = 0; i < config.len; i++)
    {
        if (buf[i + 1] == 0x22)
        {
            Information *info = get_info_panel_tp(buf[i + 2], buf[i + 3]);

            int32_t x = 0;

            if (buf[i] == 0x04)
            {
                x = buf[i + 4];
            }
            else
            {
                x = (buf[i + 4] << 8) | buf[i + 5];
            }

            x = (x + info->get_offset()) + info->get_scale();
            info->set_data(x);
        }
        else
        {
            continue;
        }
    }
}

/********************
 * Helper Functions *
 ********************/

Information *get_info_panel_tp(uint8_t byte_1, uint8_t byte_2)
{
    /* Loop through potential Information objects */
    for (uint8_t i = 0; i < layout.active_info.size(); i++)
    {
        Information *info = layout.active_info[i];                            // Get the current Information object
        if ((byte_1 == info->get_byte_1()) && (byte_2 == info->get_byte_2())) // Check if both UDS request bytes match
        {
            return info; // Return the Information object if both matches are found
        }
    }
    return nullptr; // Return null if no matches were found *extraneous data*
}

void convert_units_tp(Information *info)
{
    /* Check what type of data the Information object has */
    switch (info->get_unit_type())
    {
    case Information::PRESSURE:
        info->set_data((info->get_data_num() * 14.5038));
        break;
    case Information::SPEED:
        info->set_data((info->get_data_num() / 1.609));
        break;
    case Information::TEMP:
        info->set_data((info->get_data_num() * (9 / 5)) + 32);
        break;
    case Information::TORQUE:
        info->set_data((info->get_data_num() * 0.7375621493));
        break;
    case Information::VOLUME:
        info->set_data((info->get_data_num() / 3.785));
        break;
    default:
        break;
    }
}

void round_data_tp(Information *info)
{
    /* Check what type of data the Information object has */
    switch (info->get_unit_type())
    {
    case Information::VOLUME:
        info->set_data(round_tp(info->get_data_num(), 1)); // 1 decimal place for volume
        break;
    case Information::PRESSURE:
        if (info->get_is_metric())
        {
            info->set_data(round_tp(info->get_data_num(), 2)); // 2 decimal places for Metric
        }
        else
        {
            info->set_data(round_tp(info->get_data_num(), 1)); // 1 decimal place for Imperial
        }
        break;
    default:
        info->set_data((int)info->get_data_num()); // No decimal places for any other Information type
        break;
    }
}

float round_tp(float value, uint8_t decimal_places)
{
    /* I don't remember how this works, but it's online somewhere */
    int round_value = pow(10, decimal_places);

    float val = (int)(value * round_value + 0.5);
    return (float)(val / round_value);
}