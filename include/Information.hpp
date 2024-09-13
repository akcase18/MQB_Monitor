#include "Arduino.h"
#include <vector>

using namespace std;

/***********
 * Defines *
 ***********/

#define MAX_REFRESH 20
#define MIN_REFRESH 5000

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

private:
    /*********************
     * Private Variables *
     *********************/
    uint8_t _num_decimals_metric;   // The number of decimal places to use for Metric data
    uint8_t _num_decimals_imperial; // The number of decimal places to use for Imperial data
    uint16_t _byte_1;               // The first byte of the UDS request code
    uint16_t _byte_2;               // The second byte of the UDS request code
    int16_t _offset;                // The offset value used to decode raw data
    uint16_t _refresh_rate;         // The refresh rate of the data request
    float _data;                    // The data received
    float _scale;                   // The scaling factor used to decode raw data
    String _name;                   // The name of the Information object
    String _unit_label;             // The generated label for the units
    Unit_Type _unit_type;           // The unit type of the data
    bool _is_metric;                // The unit region of the data

public:
    /********************
     * Public Functions *
     ********************/

    Information();
    Information(Information *info);
    Information(String name);
    Information(String name, uint16_t byte_1, uint16_t byte_2);
    Information(String name, uint16_t byte_1, uint16_t byte_2, Unit_Type unit_type);
    Information(String name, uint16_t byte_1, uint16_t byte_2, Unit_Type unit_type, uint8_t num_decimals_metric, uint8_t num_decimals_imperial, uint16_t refresh_rate);
    Information(String name, uint16_t byte_1, uint16_t byte_2, int16_t offset, float scale);
    Information(String name, uint16_t byte_1, uint16_t byte_2, int16_t offset, float scale, Unit_Type unit_type);
    Information(String name, uint16_t byte_1, uint16_t byte_2, int16_t offset, float scale, Unit_Type unit_type, uint8_t num_decimals_metric, uint8_t num_decimals_imperial, uint16_t refresh_rate);

    void set_name(String name);
    String get_name();

    void set_data(float data);
    float get_data_num();
    String get_data_str();

    void set_byte_1(uint16_t byte);
    uint16_t get_byte_1();

    void set_byte_2(uint16_t byte);
    uint16_t get_byte_2();

    void set_offset(int16_t offset);
    uint16_t get_offset();

    void set_scale(float scale);
    float get_scale();

    void set_unit_type(Unit_Type type);
    Unit_Type get_unit_type();

    void set_is_metric(bool is_metric);
    bool get_is_metric();

    void set_num_decimals_metric(uint8_t decimals);
    uint8_t get_num_decimals_metric();

    void set_num_decimals_imperial(uint8_t decimals);
    uint8_t get_num_decimals_imperial();

    void set_refresh_rate(uint16_t refresh_rate);
    uint16_t get_refresh_rate();

    uint8_t calc_num_digits();

    void set_unit_label();
    String get_unit_label();

    String get_name_and_label();

private:
    /*********************
     * Private Functions *
     *********************/
};