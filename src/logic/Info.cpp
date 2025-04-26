#include "Info.hpp"

/****************
 * Constructors *
 ****************/

// Info::Info(String name, Unit_Type unit_type, uint16_t cycle_time, Request **requests, uint8_t num_requests, float (*transform)(Request **))
Info::Info(String name, Unit_Type unit_type, uint16_t cycle_time, Request **requests, uint8_t num_requests)
    : _name(name),
      _cycle_time(cycle_time),
      _requests(requests),
      _num_requests(num_requests)
    //   _transform(transform)
{
    set_unit_type(unit_type);
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

void Info::set_transform(float (*transform)(Request **))
{
    _transform = transform;
}

/*********************
 * Private Functions *
 *********************/

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