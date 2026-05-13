#include "Info.hpp"

static uint32_t info_unique_id = 0;

/*==============================================================================
 Constructors
 =============================================================================*/

Info::Info(const char *name,
           Unit_Type   unit_type,
           uint16_t    cycle_time,
           Request   **requests,
           uint8_t     num_requests)
    : _cycle_time(cycle_time)
    , _requests(requests)
    , _num_requests(num_requests)
{
    snprintf(_name, sizeof(_name), "%s", name);
    uid = info_unique_id++;
    set_unit_type(unit_type);
    set_unit_label();
}

/*==============================================================================
 Public Functions
 =============================================================================*/

const char *Info::get_name() const
{
    return _name;
}

void Info::get_name_and_label(char *buf, uint8_t len) const
{
    snprintf(buf, len, "%s %s", _name, _unit_label);
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

void Info::get_data_str(char *buf, uint8_t len) const
{
    for (uint8_t i = 0; i < _num_requests; i++)
    {
        if (!_requests[i]->get_recv())
        {
            snprintf(buf, len, "--");
        }
    }

    snprintf(buf, len, "%.*f", _is_metric ? _decimals_m : _decimals_i, _data);
}

uint16_t Info::get_cycle_time()
{
    return _cycle_time;
}

bool Info::get_metric()
{
    return _is_metric;
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

void Info::do_transform()
{
    _data = _transform(_requests);
    if (!_is_metric)
    {
        convert_units();
    }
    round_data();
}

/*==============================================================================
 Private Functions
 =============================================================================*/

void Info::set_unit_label()
{
    switch (_unit_type)
    {
        case Unit_Type::Degrees:
            snprintf(_unit_label, sizeof(_unit_label), "%s", "[°]");
            break;

        case Unit_Type::Percent:
            snprintf(_unit_label, sizeof(_unit_label), "%s", "[%]");
            break;

        case Unit_Type::Pressure:
            snprintf(_unit_label,
                     sizeof(_unit_label),
                     "%s",
                     _is_metric ? "[bar]" : "[psi]");
            break;

        case Unit_Type::RPM:
            snprintf(_unit_label, sizeof(_unit_label), "%s", "[rpm]");
            break;

        case Unit_Type::Speed:
            snprintf(_unit_label,
                     sizeof(_unit_label),
                     "%s",
                     _is_metric ? "[kph]" : "[mph]");
            break;

        case Unit_Type::Temp:
            snprintf(_unit_label,
                     sizeof(_unit_label),
                     "%s",
                     _is_metric ? "[°C]" : "[°F]");
            break;

        case Unit_Type::Torque:
            snprintf(_unit_label,
                     sizeof(_unit_label),
                     "%s",
                     _is_metric ? "[Nm]" : "[ft/lbs]");
            break;

        case Unit_Type::Volume:
            snprintf(_unit_label,
                     sizeof(_unit_label),
                     "%s",
                     _is_metric ? "[L]" : "[gal]");
            break;

        default:
            snprintf(_unit_label, sizeof(_unit_label), "%s", "");
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
    int scale = 1;
    for (uint8_t i = 0; i < decimal_places; i++)
    {
        scale *= 10;
    }

    return (int)(value * scale + 0.5f) / (float)scale;
}