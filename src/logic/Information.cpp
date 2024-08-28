#include "Information.hpp"

/*******************************
 * Information Object Creation *
 *******************************/

/***** Performance Metrics *****/
DMAMEM Information coolant_temp = Information("Coolant Temp", 0xF4, 0x05, -40, NO_SCALE, Information::TEMP, 0, 0, 2500);
DMAMEM Information engine_oil_temp = Information("Engine Oil Temp", 0x20, 0x2F, -2731.4, 0.1, Information::TEMP, 0, 0, 1000);
DMAMEM Information intake_air_temp = Information("Intake Air Temp", 0xF4, 0x0F, -40, NO_SCALE, Information::TEMP, 0, 0, 100);
DMAMEM Information charge_pressure = Information("Charge Pressure", 0x20, 0x2A, NO_OFFSET, 0.001, Information::PRESSURE, 2, 1, 100);
DMAMEM Information intake_pressure = Information("Intake Pressure", 0x3E, 0x70, NO_OFFSET, 0.001, Information::PRESSURE, 2, 1, MAX_REFRESH);
DMAMEM Information ambient_pressure = Information("Ambient Pressure", 0x13, 0xCA, NO_OFFSET, 0.001, Information::PRESSURE, 2, 1, 1000);
DMAMEM Information lambda_actual = Information("Lambda (actual)", 0x10, 0xC0, NO_OFFSET, (1 / 1024), Information::NONE, 1, 1, MAX_REFRESH);
DMAMEM Information lambda_specified = Information("Lambda (specified)", 0x29, 0x50, NO_OFFSET, (1 / 1024), Information::NONE, 1, 1, MAX_REFRESH);
DMAMEM Information turbo_speed = Information("Turbo Speed", 0x10, 0x40, NO_OFFSET, 6.11, Information::RPM, 0, 0, MIN_REFRESH);

/***** Car Information *****/
DMAMEM Information engine_rpm = Information("Engine Speed", 0x20, 0x6E, NO_OFFSET, NO_SCALE, Information::RPM, 0, 0, MAX_REFRESH);
DMAMEM Information engine_torque = Information("Engine Torque", 0x43, 0x7C, NO_OFFSET, 0.1, Information::TORQUE, 0, 0, MAX_REFRESH);
DMAMEM Information vehicle_speed = Information("Vehicle Speed", 0x20, 0x33, NO_OFFSET, 0.01, Information::SPEED, 0, 0, 100);
DMAMEM Information fuel_level = Information("Fuel", 0x22, 0x06, NO_OFFSET, NO_SCALE, Information::VOLUME, 1, 1, MIN_REFRESH);
DMAMEM Information fuel_percent = Information("Fuel", 0xF4, 0x2F, NO_OFFSET, (1 / 2.55), Information::PERCENT, 0, 0, MIN_REFRESH);
DMAMEM Information throttle_position = Information("Throttle Position", 0xF4, 0x11, NO_OFFSET, (1 / 2.55), Information::PERCENT, 0, 0, MAX_REFRESH);

/***** Diag/Monitoring *****/
DMAMEM Information misfire_sum = Information("Misfire Count", 0x29, 0x04, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
DMAMEM Information misfire_cyl_1 = Information("Misfire Count Cyl 1", 0x29, 0x66, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
DMAMEM Information misfire_cyl_2 = Information("Misfire Count Cyl 2", 0x29, 0x67, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
DMAMEM Information misfire_cyl_3 = Information("Misfire Count Cyl 3", 0x29, 0x68, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
DMAMEM Information misfire_cyl_4 = Information("Misfire Count Cyl 4", 0x29, 0x69, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
DMAMEM Information timing_corr_1 = Information("Timing Corr. Cyl 1", 0x20, 0x0A, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);
DMAMEM Information timing_corr_2 = Information("Timing Corr. Cyl 2", 0x20, 0x0B, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);
DMAMEM Information timing_corr_3 = Information("Timing Corr. Cyl 3", 0x20, 0x0C, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);
DMAMEM Information timing_corr_4 = Information("Timing Corr. Cyl 4", 0x20, 0x0D, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);

DMAMEM vector<Information *> info_list = {&coolant_temp, &engine_oil_temp, &intake_air_temp, &charge_pressure, &intake_pressure,
                                          &ambient_pressure, &lambda_actual, &lambda_specified, &turbo_speed, &engine_rpm,
                                          &engine_torque, &vehicle_speed, &fuel_level, &fuel_percent, &throttle_position,
                                          &misfire_sum, &misfire_cyl_1, &misfire_cyl_2, &misfire_cyl_3, &misfire_cyl_4,
                                          &timing_corr_1, &timing_corr_2, &timing_corr_3, &timing_corr_4};


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
    set_is_metric(true);
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

Information::Information(String name, uint16_t byte_1, uint16_t byte_2, int16_t offset, float scale, Unit_Type unit_type, uint8_t num_decimals_metric, uint8_t num_decimals_imperial, uint16_t refresh_rate)
{
    set_name(name);
    set_data(0);
    set_byte_1(byte_1);
    set_byte_2(byte_2);
    set_offset(offset);
    set_scale(scale);
    set_unit_type(unit_type);
    set_is_metric(true);
    set_unit_label();
    set_num_decimals_metric(num_decimals_metric);
    set_num_decimals_imperial(num_decimals_imperial);
    set_refresh_rate(refresh_rate);
}

/******************************
 * Public Getters and Setters *
 ******************************/

void Information::set_name(String name)
{
    _name = name;
}

String Information::get_name()
{
    return _name;
}

String Information::get_name_and_label()
{
    // TODO: Complete
    String name_and_label = _name + " " + _unit_label;
    return name_and_label;
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
    // Copy data (float) into the space allocated for the data (string)
    return String(_data);
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

void Information::set_is_metric(bool is_metric)
{
    _unit_metric = is_metric;
}
bool Information::get_is_metric()
{
    return _unit_metric;
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
    Unit_Type type = get_unit_type();
    bool is_metric = get_is_metric();
    switch (type)
    {
    case TEMP:
        if (is_metric)
        {
            _unit_label = "[°C]";
        }
        else
        {
            _unit_label = "[°F]";
        }
        break;

    case PRESSURE:
        if (is_metric)
        {
            _unit_label = "[bar]";
        }
        else
        {
            _unit_label = "[psi]";
        }
        break;

    case SPEED:
        if (is_metric)
        {
            _unit_label = "[kph]";
        }
        else
        {
            _unit_label = "[mph]";
        }
        break;

    case TORQUE:
        if (is_metric)
        {
            _unit_label = "[Nm]";
        }
        else
        {
            _unit_label = "[ft/lbs]";
        }
        break;

    case VOLUME:
        if (is_metric)
        {
            _unit_label = "[L]";
        }
        else
        {
            _unit_label = "[gal]";
        }
        break;

    case RPM:
        _unit_label = "[rpm]";
        break;

    case DEGREES:
        _unit_label = "[°]";
        break;

    case PERCENT:
        _unit_label = "[%]";
        break;

    case NONE:

    default:
        _unit_label = "";
        break;
    }
}
String Information::get_unit_label()
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
    bool is_metric = get_is_metric();
    /* Checks for number of spaces to allocate for decimal places and decimal point */
    if (is_metric)
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
