#include "Information.hpp"

Information::Information()
{
    set_name("default");
    set_data(0);
    set_byte_1(0x00);
    set_byte_2(0x00);
    set_offset(NO_OFFSET);
    set_scale(NO_SCALE);
    set_unit_type(Information::NONE);
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
    set_is_metric(info->get_is_metric());
    set_unit_label();
    set_num_decimals_metric(info->get_num_decimals_metric());
    set_num_decimals_imperial(info->get_num_decimals_imperial());
    set_refresh_rate(info->get_refresh_rate());
}

Information::Information(String name, uint16_t byte_1, uint16_t byte_2, int16_t offset, float scale, Information::Unit_Type unit_type, uint8_t num_decimals_metric, uint8_t num_decimals_imperial, uint16_t refresh_rate)
{
    set_name(name);
    set_data(0);
    set_byte_1(byte_1);
    set_byte_2(byte_2);
    set_offset(offset);
    set_scale(scale);
    set_unit_type(unit_type);
    set_unit_label();
    set_num_decimals_metric(num_decimals_metric);
    set_num_decimals_imperial(num_decimals_imperial);
    set_refresh_rate(refresh_rate);
}

void Information::set_name(String name)
{
    _name = name;
}
String Information::get_name()
{
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
String Information::get_data_str()
{
    String data_str;
    if (_is_metric)
    {
        data_str = String(_data, get_num_decimals_metric());
    }
    else
    {
        data_str = String(_data, get_num_decimals_imperial());
    }
    return data_str;
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

void Information::set_unit_type(Information::Unit_Type type)
{
    _unit_type = type;
}
Information::Unit_Type Information::get_unit_type()
{
    return _unit_type;
}

void Information::set_num_decimals_metric(uint8_t decimals)
{
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

uint8_t Information::calc_num_digits()
{
    uint8_t num_digits = 1;
    if (_data < 0)
    {
        num_digits++; // Add space for negative sign
    }
    uint16_t temp = abs(_data);
    if (_is_metric)
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
    else
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

/*********************
 * Private Functions *
 *********************/

void Information::set_is_metric(bool is_metric)
{
    _is_metric = is_metric;
}
bool Information::get_is_metric()
{
    return _is_metric;
}

void Information::set_unit_label()
{
    Information::Unit_Type type = Information::get_unit_type();
    switch (type)
    {
    case Information::TEMP:
        if (get_is_metric())
        {
            _unit_label = "[°C]";
        }
        else
        {
            _unit_label = "[°F]";
        }
        break;

    case Information::PRESSURE:
        if (get_is_metric())
        {
            _unit_label = "[bar]";
        }
        else
        {
            _unit_label = "[psi]";
        }
        break;

    case Information::SPEED:
        if (get_is_metric())
        {
            _unit_label = "[kph]";
        }
        else
        {
            _unit_label = "[mph]";
        }
        break;

    case Information::TORQUE:
        if (get_is_metric())
        {
            _unit_label = "[Nm]";
        }
        else
        {
            _unit_label = "[ft/lbs]";
        }
        break;

    case Information::VOLUME:
        if (get_is_metric())
        {
            _unit_label = "[L]";
        }
        else
        {
            _unit_label = "[gal]";
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
String Information::get_unit_label()
{
    return _unit_label;
}

String Information::get_name_and_label()
{
    return _name;
}