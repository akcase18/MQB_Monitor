#include "Information.hpp"

/*********************************
 * Information Object Definition *
 *********************************/

/***** Performance Metrics *****/
Information coolant_temp = Information("Coolant Temp",Information::TEMP, 0, 0, 2000);      // Works
Information engine_oil_temp = Information("Engine Oil Temp", Information::TEMP, 0, 0, 1000); // Works
Information intake_air_temp = Information("Intake Air Temp", Information::TEMP, 0, 0, 100); // Works
Information charge_pressure = Information("Charge Pressure", Information::PRESSURE, 2, 1, 100);
Information intake_pressure = Information("Intake Pressure", Information::PRESSURE, 2, 1, MAX_REFRESH); // Works
Information ambient_pressure = Information("Amb. Pressure", Information::PRESSURE, 2, 1, 1000);        // Works
Information actual_intake_pressure = Information("Act. Intake Pressure", Information::PRESSURE, 2, 1, MAX_REFRESH);
Information lambda_actual = Information("Lambda (actual)", Information::NONE, 1, 1, MAX_REFRESH);
Information lambda_specified = Information("Lambda (specified)", Information::NONE, 1, 1, MAX_REFRESH);
Information turbo_speed = Information("Turbo Speed", Information::RPM, 0, 0, MIN_REFRESH);

/***** Car Information *****/
Information engine_rpm = Information("Engine Speed", Information::RPM, 0, 0, MAX_REFRESH);
Information engine_torque = Information("Engine Torque", Information::TORQUE, 0, 0, MAX_REFRESH);
Information vehicle_speed = Information("Vehicle Speed", Information::SPEED, 0, 0, 100); // Works
Information fuel_level = Information("Fuel", Information::VOLUME, 1, 1, MIN_REFRESH);
Information fuel_percent = Information("Fuel", Information::PERCENT, 0, 0, MIN_REFRESH);
Information throttle_position = Information("Throttle Position", Information::PERCENT, 0, 0, MAX_REFRESH); // Works

/***** Diag/Monitoring *****/
Information misfire_sum = Information("Misfire Count", Information::NONE, 0, 0, 500);         // Works
Information misfire_cyl_1 = Information("Misfire Count Cyl 1", Information::NONE, 0, 0, 500); // Works
Information misfire_cyl_2 = Information("Misfire Count Cyl 2", Information::NONE, 0, 0, 500); // Works
Information misfire_cyl_3 = Information("Misfire Count Cyl 3", Information::NONE, 0, 0, 500); // Works
Information misfire_cyl_4 = Information("Misfire Count Cyl 4", Information::NONE, 0, 0, 500); // Works
Information timing_corr_1 = Information("Timing Corr. Cyl 1", Information::DEGREES, 0, 0, 100);
Information timing_corr_2 = Information("Timing Corr. Cyl 2", Information::DEGREES, 0, 0, 100);
Information timing_corr_3 = Information("Timing Corr. Cyl 3", Information::DEGREES, 0, 0, 100);
Information timing_corr_4 = Information("Timing Corr. Cyl 4", Information::DEGREES, 0, 0, 100);

/***** Information Object List/Vector *****/
vector<Information *> info_list = {&coolant_temp, &engine_oil_temp, &intake_air_temp, &charge_pressure, &intake_pressure,
                                   &ambient_pressure, &lambda_actual, &lambda_specified, &turbo_speed, &engine_rpm,
                                   &engine_torque, &vehicle_speed, &fuel_level, &fuel_percent, &throttle_position, &misfire_sum,
                                   &misfire_cyl_1, &misfire_cyl_2, &misfire_cyl_3, &misfire_cyl_4, &timing_corr_1, &timing_corr_2,
                                   &timing_corr_3, &timing_corr_4};

/****************
 * Constructors *
 ****************/

Information::Information(Information *info)
{
    _name = info->get_name();
    set_data(info->get_data_num());
    set_unit_type(info->get_unit_type());
    set_is_metric(info->get_is_metric());
    set_unit_label();
    set_num_decimals_metric(info->get_num_decimals_metric());
    set_num_decimals_imperial(info->get_num_decimals_imperial());
    _refresh_rate = info->get_refresh_rate();
}

Information::Information(String name, Information::Unit_Type unit_type, uint8_t num_decimals_metric, uint8_t num_decimals_imperial, uint16_t refresh_rate)
{
    _name = name;
    set_data(0);
    set_unit_type(unit_type);
    set_unit_label();
    set_num_decimals_metric(num_decimals_metric);
    set_num_decimals_imperial(num_decimals_imperial);
    _refresh_rate = refresh_rate;
}

/******************************
 * Public Getters and Setters *
 ******************************/

/***** Name *****/
String Information::get_name()
{
    return _name;
}
String Information::get_name_and_label()
{
    return _name + " " + _unit_label;
}

/***** Data *****/

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

/**** Unit Type *****/

void Information::set_unit_type(Information::Unit_Type type)
{
    _unit_type = type;
}
Information::Unit_Type Information::get_unit_type()
{
    return _unit_type;
}

/**** Number of Decimals (Metric) *****/

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

/**** Number of Decimals (Imperial) *****/

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

/**** Refresh Rate *****/

uint16_t Information::get_refresh_rate()
{
    return _refresh_rate;
}

/***** Is Metric Flag *****/

void Information::set_is_metric(bool is_metric)
{
    _is_metric = is_metric;
    set_unit_label();
}
bool Information::get_is_metric()
{
    return _is_metric;
}

/*******************************
 * Private Getters and Setters *
 *******************************/

/***** Unit Label *****/

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

/****************************
 * Private Helper Functions *
 ****************************/

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