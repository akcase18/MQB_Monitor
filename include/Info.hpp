#pragma once

#include "Request.hpp"

enum class Unit_Type
{
    None,
    Degrees,
    Percent,
    Pressure,
    RPM,
    Speed,
    Temp,
    Torque,
    Volume
};

class Info
{
private:
    /* Private Variables */
    String _name;         // Name of the Info object
    String _unit_label;   // The generated label for the units
    uint8_t _decimals_m;  // Metric data decimal places
    uint8_t _decimals_i;  // Imperial data decimal places
    uint16_t _cycle_time; // Update cycle time (in ms)
    float _data = 0.0;    // Data value
    Unit_Type _unit_type; // Data unit type
    Request **_requests;
    uint8_t _num_requests;
    float (*_transform)(Request **);
    bool _is_metric = false;

public:
    /* Public Variables */
    uint32_t uid; // Unique ID fo the Info object

    /**
     * Constructor for Info objects.
     * Creates a new Info object that will consolidate data
     * from the car into human-readable data to display.
     *
     * @param name
     * @param unit_type
     * @param cyle_time
     * @param requests
     * @param num_requests
     * @param transform
     */
    Info(String name, Unit_Type unit_type, uint16_t cycle_time, Request **requests, uint8_t num_requests);

    String get_name();

    String get_name_and_label();

    /**
     * Sets the unit type of the Info object.
     *
     * @param unit_type the unit type of the data.
     */
    void set_unit_type(Unit_Type unit_type);

    float get_data();

    String get_data_str();

    uint16_t get_cycle_time();

    bool get_metric();

    Request **get_requests();
    uint8_t get_num_requests();

    void set_transform(float (*transform)(Request **));
    void do_transform();

private:
    /* Private Functions */

    /**
     * Sets the unit label based on the type of data.
     */
    void set_unit_label();

    /**
     * Sets the number of decimals to use for both metric and
     * imperial data, based on the type of data.
     */
    void set_decimals();

    /**
     * Converts the data metric to imperial units.
     */
    void convert_units();

    /**
     * Automatically rounds the data based on the data type.
     */
    void round_data();

    /**
     * Rounding helper function to round a given value to a certain number
     * of decimal places.
     *
     * @param value the value to round
     * @param decimal_places the number of decimal places to round the given value to
     *
     * @return the rounded value with the correct number of decimal places
     */
    float round(float value, uint8_t decimal_places);
};