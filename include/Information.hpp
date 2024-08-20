#ifndef INFORMATION_H
#define INFORMATION_H

/**
 * This is the Information class.
 *
 * The Information class holds data specific to each bit of information
 * that is requested from the car. This includes but is not limited to:
 * -> CAN-specific information such as scale, offset, and request bytes
 * -> Current values
 * -> Information name
 * -> Displaying specifications
 * -> Refresh rate
 * -> Etc.
 * The goal for each Information object is to be used in Panel objects,
 * where they can then be swapped out for other Information objects as
 * different configurations are chosen.
 *
 * Used in:
 * -> Panel
 */

#include "Arduino.h"
#include <vector>
#include "Globals.hpp"

using namespace std;

/********************
 * Global Constants *
 ********************/

#define MAX_REFRESH 20   // The fastest refresh time for an Information object (20 ms)
#define MIN_REFRESH 5000 // The slowest refresh time for an Information object (5000 ms)
#define NO_OFFSET 0      // Constant for when there is no offset value
#define NO_SCALE 1       // Constant for when there is no scaling value

class Information
{
private:
    /*********************
     * Private Constants *
     *********************/

public:
    /********************
     * Public Constants *
     ********************/

    /**
     * Represents the different types that each data can be
     */
    enum Unit_Type
    {
        NONE = 0,
        DEGREES,
        PERCENT,
        PRESSURE,
        RPM,
        SPEED,
        TEMP,
        TORQUE,
        VOLUME
    };

    /**
     * Represents the "region" that the data will be in
     */
    enum Unit_Region
    {
        METRIC = 0,
        IMPERIAL
    };

private:
    /*********************
     * Private Variables *
     *********************/
    char _name[40];                 // The name of the Information object
    float _data;                    // The data received
    char _data_str[10];             // The data received as a string
    uint16_t _byte_1;               // The first byte of the UDS request code
    uint16_t _byte_2;               // The second byte of the UDS request code
    int16_t _offset;                // The offset value used to decode raw data
    float _scale;                   // The scaling factor used to decode raw data
    Unit_Type _unit_type;           // The unit type of the data
    Unit_Region _unit_region;       // The unit region of the data
    char *_unit_label;              // The generated label for the units
    uint8_t _num_decimals_metric;   // The number of decimal places to use for Metric data
    uint8_t _num_decimals_imperial; // The number of decimal places to use for Imperial data
    uint16_t _refresh_rate;         // The refresh rate of the data request

    /*********************
     * Private Functions *
     *********************/

    /**
     * Initializes all 
    */
    void init_info();

    /**
     * Calculates the number of digits in a number. This is used within the
     * "get_data_str()" function to figure out how long the char array should
     * be to hold all digits, decimal points, and negative signs (if applicable).
     *
     * @return The number of "digits" in the data variable
     */
    uint8_t calc_num_digits();

    /**
     * Sets the unit label of the data. This will change based on whether that Information
     * object is using Metric or Imperial data formatting, as well as the Unit Type of the
     * data.
     *
     * @note Format: [label]
     */
    void set_unit_label();

    /**
     * Gets the unit label of the data.
     *
     * @note Format: [label]
     */
    char *get_unit_label();

public:
    /********************
     * Public Variables *
     ********************/

    static vector<Information *> information_list; // List of all information objects

    /********************
     * Public Functions *
     ********************/

    /**
     * Default constructor for Information objects. Initializes everything to 0 or None.
     */
    Information();

    /**
     * Constructor to pass an existing Information object into a new one. This will copy
     * all parameters and variables from the given Information object and assign them to the new
     * Information object.
     *
     * @param info A pointer to an already-creted Information object
     *
     * @note This is used to assign Information objects to Information Panels in the GUI
     */
    Information(Information *info);

    /**
     * Constructor to fully define a new Information object. This will default the Unit_Region
     * to Metric and the data held within the Information object to 0.
     *
     * @param name An array of characters for the name of the Information object.
     * @param byte_1 The first byte of the UDS request code in hex.
     * @param byte_2 The second byte of the UDS request code in hex.
     * @param offset The value by which the incoming raw data should be shifting by to decode it.
     * @param scale The scaling factor for the incoming raw data.
     * @param unit_type The type of data that the Information object holds. Type of Unit_Type.
     * @param num_decimals_metric The number of decimal places for Metric data format.
     * @param num_decimals_imperial The number of decimal places for Imperial data format.
     * @param refresh_rate The cycle time of the Information object in ms. Smaller is faster.
     */
    Information(char *name, uint16_t byte_1, uint16_t byte_2, int16_t offset, float scale, Unit_Type unit_type, uint8_t num_decimals_metric, uint8_t num_decimals_imperial, uint16_t refresh_rate);

    /**
     * Sets the name of the Information object.
     *
     * @param name A char array of the new name of the Information object.
     */
    void set_name(char *name);
    /**
     * Gets the name as a char array.
     *
     * @return A char array.
     */
    char *get_name();
    /**
     * Gets the name with the data label appended onto it as a char array.
     *
     * @return A char array.
     *
     * @note Format: {Name} {[Unit Label]}
     */
    char *get_name_and_label();

    /**
     * Sets the data value of the Information object.
     *
     * @param data The new data value as a float.
     */
    void set_data(float data);
    /**
     * Gets the data as a float.
     *
     * @return A float.
     */
    float get_data_num();
    /**
     * Gets the data as a string.
     *
     * @return A char array.
     */
    char *get_data_str();

    /**
     * Sets the value of the first byte of the UDS request code.
     *
     * @param byte The first byte of the UDS request code in hex.
     */
    void set_byte_1(uint16_t byte);
    /**
     * Gets the value of the first byte of the UDS request code.
     *
     * @return The first byte of the UDS request code in hex.
     */
    uint16_t get_byte_1();

    /**
     * Sets the value of the second byte of the UDS request code.
     *
     * @param byte The second byte of the UDS request code in hex.
     */
    void set_byte_2(uint16_t byte);
    /**
     * Gets the value of the second byte of the UDS request code.
     *
     * @return THe second byte of the UDS request code in hex.
     */
    uint16_t get_byte_2();

    /**
     * Sets the offset value used to decode the raw data.
     *
     * @param offset The offset value as a 16-bit value.
     */
    void set_offset(int16_t offset);
    /**
     * Gets the offset value used to decode the raw data.
     *
     * @return The offset value as a 16-bit value.
     */
    int16_t get_offset();

    /**
     * Sets the scaling factor used to decode the raw data.
     *
     * @param scale The scaling factor as a float.
     */
    void set_scale(float scale);
    /**
     * Gets the scaling factor used to decode the raw data.
     *
     * @return The scaling factor as a float.
     */
    float get_scale();

    /**
     * Sets the Unit Type of the Information object.
     *
     * @param type The new Unit Type as a Unit_Type enum.
     */
    void set_unit_type(Unit_Type type);
    /**
     * Gets the Unit Type of the Information object.
     *
     * @return The new Unit Type as a Unit_Type enum.
     */
    Unit_Type get_unit_type();

    /**
     * Sets the Unit Region of the Information object.
     *
     * @param region The new Unit Region as a Unit_Region enum.
     */
    void set_unit_region(Unit_Region region);
    /**
     * Gets the Unit Region of the Information object.
     *
     * @return The new Unit Region as a Unit_Region enum.
     */
    Unit_Region get_unit_region();

    /**
     * Sets the number of decimal places to use when using Metric data.
     *
     * @param decimals The number of decimal places.
     */
    void set_num_decimals_metric(uint8_t decimals);
    /**
     * Gets the number of decimal places to use when using Metric data.
     *
     * @return The number of decimal places.
     */
    uint8_t get_num_decimals_metric();

    /**
     * Sets the number of decimal places to use when using Imperial data.
     *
     * @param decimals The number of decimal places.
     */
    void set_num_decimals_imperial(uint8_t decimals);
    /**
     * Gets the number of decimal places to use when using Imperial data.
     *
     * @return The number of decimal places.
     */
    uint8_t get_num_decimals_imperial();

    /**
     * Sets the refresh rate of the data coming in (in ms).
     *
     * @param refresh_rate The new refresh rate in ms.
     */
    void set_refresh_rate(uint16_t refresh_rate);
    /**
     * Gets the refresh rate of the data coming in (in ms).
     *
     * @return The refresh rate in ms.
     */
    uint16_t get_refresh_rate();
};

/*******************************
 * Information Object Creation *
 *******************************/

/***** Performance Metrics *****/
static Information coolant_temp = Information("Coolant Temp", 0xF4, 0x05, -40, NO_SCALE, Information::TEMP, 0, 0, 2500);
static Information engine_oil_temp = Information("Engine Oil Temp", 0x20, 0x2F, -2731.4, 0.1, Information::TEMP, 0, 0, 1000);
static Information intake_air_temp = Information("Intake Air Temp", 0xF4, 0x0F, -40, NO_SCALE, Information::TEMP, 0, 0, 100);
static Information charge_pressure = Information("Charge Pressure", 0x20, 0x2A, NO_OFFSET, 0.001, Information::PRESSURE, 2, 1, 100);
static Information intake_pressure = Information("Intake Pressure", 0x3E, 0x70, NO_OFFSET, 0.001, Information::PRESSURE, 2, 1, MAX_REFRESH);
static Information ambient_pressure = Information("Ambient Pressure", 0x13, 0xCA, NO_OFFSET, 0.001, Information::PRESSURE, 2, 1, 1000);
static Information lambda_actual = Information("Lambda (actual)", 0x10, 0xC0, NO_OFFSET, (1 / 1024), Information::NONE, 1, 1, MAX_REFRESH);
static Information lambda_specified = Information("Lambda (specified)", 0x29, 0x50, NO_OFFSET, (1 / 1024), Information::NONE, 1, 1, MAX_REFRESH);
static Information turbo_speed = Information("Turbo Speed", 0x10, 0x40, NO_OFFSET, 6.11, Information::RPM, 0, 0, MIN_REFRESH);

/***** Car Information *****/
static Information engine_rpm = Information("Engine Speed", 0x20, 0x6E, NO_OFFSET, NO_SCALE, Information::RPM, 0, 0, MAX_REFRESH);
static Information engine_torque = Information("Engine Torque", 0x43, 0x7C, NO_OFFSET, 0.1, Information::TORQUE, 0, 0, MAX_REFRESH);
static Information vehicle_speed = Information("Vehicle Speed", 0x20, 0x33, NO_OFFSET, 0.01, Information::SPEED, 0, 0, 100);
static Information fuel_level = Information("Fuel", 0x22, 0x06, NO_OFFSET, NO_SCALE, Information::VOLUME, 1, 1, MIN_REFRESH);
static Information fuel_percent = Information("Fuel", 0xF4, 0x2F, NO_OFFSET, (1 / 2.55), Information::PERCENT, 0, 0, MIN_REFRESH);
static Information throttle_position = Information("Throttle Position", 0xF4, 0x11, NO_OFFSET, (1 / 2.55), Information::PERCENT, 0, 0, MAX_REFRESH);

/***** Diag/Monitoring *****/
static Information misfire_sum = Information("Misfire Count", 0x29, 0x04, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
static Information misfire_cyl_1 = Information("Misfire Count Cyl 1", 0x29, 0x66, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
static Information misfire_cyl_2 = Information("Misfire Count Cyl 2", 0x29, 0x67, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
static Information misfire_cyl_3 = Information("Misfire Count Cyl 3", 0x29, 0x68, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
static Information misfire_cyl_4 = Information("Misfire Count Cyl 4", 0x29, 0x69, NO_OFFSET, NO_SCALE, Information::NONE, 0, 0, 500);
static Information timing_corr_1 = Information("Timing Corr. Cyl 1", 0x20, 0x0A, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);
static Information timing_corr_2 = Information("Timing Corr. Cyl 2", 0x20, 0x0B, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);
static Information timing_corr_3 = Information("Timing Corr. Cyl 3", 0x20, 0x0C, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);
static Information timing_corr_4 = Information("Timing Corr. Cyl 4", 0x20, 0x0D, NO_OFFSET, 0.01, Information::DEGREES, 0, 0, 100);

#endif