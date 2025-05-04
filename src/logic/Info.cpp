#include "Info.hpp"

/****************
 * Constructors *
 ****************/

Info::Info(String name, Unit_Type unit_type, uint16_t cycle_time, Request **requests, uint8_t num_requests)
    : _name(name),
      _cycle_time(cycle_time),
      _requests(requests),
      _num_requests(num_requests)
{
    set_unit_type(unit_type);
    set_unit_label();
}

/********************
 * Public Functions *
 ********************/

String Info::get_name()
{
    return _name;
}

String Info::get_name_and_label()
{
    return _name + " " + _unit_label;
}

void Info::set_unit_type(Unit_Type unit_type)
{
    _unit_type = unit_type;
    set_decimals();
}

float Info::get_data()
{
    return _data;
}

String Info::get_data_str()
{
    return String(_data, (_is_metric) ? _decimals_m : _decimals_i);
}

Request **Info::get_requests()
{
    return _requests;
}

uint8_t Info::get_num_requests()
{
    return _num_requests;
}

void Info::set_transform(float (*transform)(Request **))
{
    _transform = transform;
}

/*********************
 * Private Functions *
 *********************/

void Info::set_unit_label()
{
    switch (_unit_type)
    {
    case Unit_Type::Degrees:
        _unit_label = "[°]";
        break;

    case Unit_Type::Percent:
        _unit_label = "[%]";
        break;

    case Unit_Type::Pressure:
        _unit_label = _is_metric ? "[bar]" : "[psi]";
        break;

    case Unit_Type::RPM:
        _unit_label = "[rpm]";
        break;

    case Unit_Type::Speed:
        _unit_label = _is_metric ? "[kph]" : "[mph]";
        break;

    case Unit_Type::Temp:
        _unit_label = _is_metric ? "[°C]" : "[°F]";
        break;

    case Unit_Type::Torque:
        _unit_label = _is_metric ? "[Nm]" : "[ft/lbs]";
        break;

    case Unit_Type::Volume:
        _unit_label = _is_metric ? "[L]" : "[gal]";
        break;

    default:
        _unit_label = "";
        break;
    }
}

void Info::set_decimals()
{
    if (_unit_type == Unit_Type::Pressure)
    {
        _decimals_m = 2;
        _decimals_i = 1;
    }
    else if (_unit_type == Unit_Type::Volume)
    {
        _decimals_m = 1;
        _decimals_i = 1;
    }
    else
    {
        _decimals_m = 0;
        _decimals_i = 0;
    }

    switch (_unit_type)
    {
    case Unit_Type::Pressure:
        _decimals_m = 2;
        _decimals_i = 1;
        break;

    case Unit_Type::Volume:
        _decimals_m = 1;
        _decimals_i = 1;
        break;

    case Unit_Type::Degrees:
    case Unit_Type::Percent:
    case Unit_Type::RPM:
    case Unit_Type::Speed:
    case Unit_Type::Temp:
    case Unit_Type::Torque:
    case Unit_Type::None:
    default:
        _decimals_m = 0;
        _decimals_i = 0;
        break;
    }
}

void Info::convert_units()
{
    switch (_unit_type)
    {
    case Unit_Type::Pressure:
        _data = _data * 14.5038;
        break;

    case Unit_Type::Speed:
        _data = _data / 1.609;
        break;

    case Unit_Type::Temp:
        _data = _data * 1.8 + 32;
        break;

    case Unit_Type::Torque:
        _data = _data * 0.7375621493;
        break;

    case Unit_Type::Volume:
        _data = _data / 3.785;
        break;

    case Unit_Type::None:
    default:
        break;
    }
}

void Info::round_data()
{
    switch (_unit_type)
    {
    case Unit_Type::Volume:
        _data = round(_data, 1);
        break;

    case Unit_Type::Pressure:
        if (_is_metric)
        {
            _data = round(_data, 2);
        }
        else
        {
            _data = round(_data, 1);
        }
        break;

    default:
        _data = (int)_data; // No decimal places
        break;
    }
}

float Info::round(float value, uint8_t decimal_places)
{
    /* I don't remember how this works, but it's online somewhere */
    int round_value = pow(10, decimal_places);

    float val = (int)(value * round_value + 0.5);
    return (float)(val / round_value);
}