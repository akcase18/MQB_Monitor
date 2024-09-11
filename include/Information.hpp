#include "Arduino.h"

using namespace std;

/***********
 * Defines *
 ***********/

#define MAX_REFRESH 20
#define MIN_REFRESH 5000

class Information
{
public:
    enum Unit_Type
    {
        TEMP = 0,
        PRESSURE,
        SPEED,
        TORQUE,
        VOLUME,
        RPM,
        DEGREES,
        PERCENT,
        NONE
    };
    enum Unit_Region
    {
        IMPERIAL = 0,
        METRIC
    };

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

    void set_unit_region(Unit_Region region);
    Unit_Region get_unit_region();

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
    String _name;
    float _data;
    String _data_str;
    uint16_t _byte_1;
    uint16_t _byte_2;
    int16_t _offset;
    float _scale;
    Unit_Type _unit_type;
    Unit_Region _unit_region;
    String _unit_label;
    uint8_t _num_decimals_metric;
    uint8_t _num_decimals_imperial;
    uint16_t _refresh_rate;
};