#include "Information.hpp"

/****************
 * Constructors *
 ****************/

Information::Information()
{
    set_name("default");
    set_data(0);
    set_byte_1(0x00);
    set_byte_2(0x00);
    set_offset(0);
    set_scale(1);
    set_unit_type(Information::NONE);
    set_unit_region(Information::METRIC);
    set_unit_label();
    set_num_decimals_metric(0);
    set_num_decimals_imperial(0);
    set_refresh_rate(100);
}

Information::Information(Information *info)
{
    set_name(info->get_name());
    set_data(info->get_data_num());
    set_byte_1(info->get_byte_1());
    set_byte_2(info->get_byte_2());
    set_offset(info->get_offset());
    set_scale(info->get_scale());
    set_unit_type(info->get_unit_type());
    set_unit_region(info->get_unit_region());
    set_unit_label();
    set_num_decimals_metric(info->get_num_decimals_metric());
    set_num_decimals_imperial(info->get_num_decimals_imperial());
    set_refresh_rate(info->get_refresh_rate());
}

Information::Information(char *name, uint16_t byte_1, uint16_t byte_2, int16_t offset, float scale, Unit_Type unit_type, uint8_t num_decimals_metric, uint8_t num_decimals_imperial, uint16_t refresh_rate)
{
    set_name(name);
    set_data(0);
    set_byte_1(byte_1);
    set_byte_2(byte_2);
    set_offset(offset);
    set_scale(scale);
    set_unit_type(unit_type);
    set_unit_region(Information::METRIC);
    set_unit_label();
    set_num_decimals_metric(num_decimals_metric);
    set_num_decimals_imperial(num_decimals_imperial);
    set_refresh_rate(refresh_rate);
}

/******************************
 * Public Getters and Setters *
 ******************************/

void Information::set_name(char *name)
{
    // Copy the char array into the space allocated for name
    snprintf(_name, sizeof(_name), "%s", name);
}

char *Information::get_name()
{
    return _name;
}

char *Information::get_name_and_label()
{
    // TODO: Complete
    return _name;
}

void Information::set_data(float data)
{
    _data = data;
}

float Information::get_data_num()
{
    return _data;
}

char *Information::get_data_str()
{
    // Copy data (float) into the space allocated for the data (string)
    snprintf(_data_str, calc_num_digits(), "%f", _data);
    return _data_str;
}

void Information::set_byte_1(uint16_t byte)
{
    _byte_1 = byte;
}

uint16_t Information::get_byte_1()
{
    return _byte_1;
}

void Information::set_byte_2(uint16_t byte)
{
    _byte_2 = byte;
}

uint16_t Information::get_byte_2()
{
    return _byte_2;
}

void Information::set_offset(int16_t offset)
{
    _offset = offset;
}

int16_t Information::get_offset()
{
    return _offset;
}

void Information::set_scale(float scale)
{
    _scale = scale;
}

float Information::get_scale()
{
    return _scale;
}

void Information::set_unit_type(Unit_Type type)
{
    _unit_type = type;
}
Information::Unit_Type Information::get_unit_type()
{
    return _unit_type;
}

void Information::set_unit_region(Unit_Region region)
{
    _unit_region = region;
}
Information::Unit_Region Information::get_unit_region()
{
    return _unit_region;
}

void Information::set_num_decimals_metric(uint8_t decimals)
{
    // Clamp to only 2 decimal places
    if (decimals > 2)
    {
        decimals = 2;
    }
    _num_decimals_metric = decimals;
}
uint8_t Information::get_num_decimals_metric()
{
    return _num_decimals_metric;
}

void Information::set_num_decimals_imperial(uint8_t decimals)
{
    // Clamp to only 2 decimal places
    if (decimals > 2)
    {
        decimals = 2;
    }
    _num_decimals_imperial = decimals;
}
uint8_t Information::get_num_decimals_imperial()
{
    return _num_decimals_imperial;
}

void Information::set_refresh_rate(uint16_t refresh_rate)
{
    _refresh_rate = refresh_rate;
}
uint16_t Information::get_refresh_rate()
{
    return _refresh_rate;
}

/*******************************
 * Private Getters and Setters *
 *******************************/

void Information::set_unit_label()
{
    Information::Unit_Type type = Information::get_unit_type();
    Information::Unit_Region region = Information::get_unit_region();
    switch (type)
    {
    case Information::TEMP:
        if (region == Information::IMPERIAL)
        {
            _unit_label = "[°F]";
        }
        else if (region == Information::METRIC)
        {
            _unit_label = "[°C]";
        }
        else
        {
            // Error
        }
        break;

    case Information::PRESSURE:
        if (region == Information::IMPERIAL)
        {
            _unit_label = "[psi]";
        }
        else if (region == Information::METRIC)
        {
            _unit_label = "[bar]";
        }
        else
        {
            // Error
        }
        break;

    case Information::SPEED:
        if (region == Information::IMPERIAL)
        {
            _unit_label = "[mph]";
        }
        else if (region == Information::METRIC)
        {
            _unit_label = "[kph]";
        }
        else
        {
            // Error
        }
        break;

    case Information::TORQUE:
        if (region == Information::IMPERIAL)
        {
            _unit_label = "[ft/lbs]";
        }
        else if (region == Information::METRIC)
        {
            _unit_label = "[Nm]";
        }
        else
        {
            // Error
        }
        break;

    case Information::VOLUME:
        if (region == Information::IMPERIAL)
        {
            _unit_label = "[gal]";
        }
        else if (region == Information::METRIC)
        {
            _unit_label = "[L]";
        }
        else
        {
            // Error
        }
        break;

    case Information::RPM:
        _unit_label = "[rpm]";
        break;

    case Information::DEGREES:
        _unit_label = "[°]";
        break;

    case Information::PERCENT:
        _unit_label = "[%]";
        break;

    case Information::NONE:

    default:
        _unit_label = "";
        break;
    }
}
char *Information::get_unit_label()
{
    return _unit_label;
}

/****************************
 * Private Helper Functions *
 ****************************/

uint8_t Information::calc_num_digits()
{
    uint8_t num_digits = 1;
    if (_data < 0) // Check if data is negative
    {
        num_digits++; // Add space for negative sign
    }
    uint16_t temp = abs(_data);
    /* Checks for number of spaces to allocate for decimal places and decimal point */
    if (_unit_region == IMPERIAL)
    {
        if (get_num_decimals_imperial() > 0)
        {
            num_digits++; // Add space for decimal point
            for (int i = 0; i < get_num_decimals_imperial(); i++)
            {
                num_digits++;
            }
        }
    }
    else if (_unit_region == METRIC)
    {
        if (get_num_decimals_metric() > 0)
        {
            num_digits++; // Add space for decimal point
            for (int i = 0; i < get_num_decimals_metric(); i++)
            {
                num_digits++;
            }
        }
    }
    /* Check for number of spaces to allocate for full number digits */
    if (int(log10(temp)) > 0)
    {
        for (int i = 0; i < int(log10(temp)) + 1; i++)
        {
            num_digits++; // Add space for each digit before the decimal place
        }
    }
    else
    {
        num_digits++;
    }
    if (_data == 0)
    {
        return 2;
    }
    return num_digits;
}
