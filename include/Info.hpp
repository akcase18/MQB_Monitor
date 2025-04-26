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
    // Info(String name, Unit_Type unit_type, uint16_t cycle_time, Request **requests, uint8_t num_requests, float (*transform)(Request **));

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

    void set_transform(float (*transform)(Request **));

private:
    /* Private Functions */

    /**
     * Sets the number of decimals to use for both metric and
     * imperial data, based on the type of data.
     */
    void set_decimals();
};