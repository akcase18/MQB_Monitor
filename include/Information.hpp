#include "Request.hpp"

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
        LAMBDA,
        PERCENT,
        PRESSURE,
        RPM,
        SPEED,
        SUM,
        TEMP,
        TORQUE,
        VOLUME
    };

private:
    /*********************
     * Private Variables *
     *********************/
    uint32_t uid;                   // Unique ID for the Information object
    Request **_requests;            // Request objects associated with the Information object
    volatile uint8_t _num_requests; // Number of Request objects
    uint8_t _num_decimals_metric;   // The number of decimal places to use for Metric data
    uint8_t _num_decimals_imperial; // The number of decimal places to use for Imperial data
    uint16_t _refresh_rate;         // The refresh rate of the data request
    float _data;                    // Data held by the Information object
    String _name;                   // The name of the Information object
    String _unit_label;             // The generated label for the units
    Unit_Type _unit_type;           // The unit type of the data
    bool _is_metric = true;         // The unit region of the data

    /*********************
     * Private Functions *
     *********************/

    /**
     * Calculates the number of digits in a number. This is used within the
     * "get_data_str()" function to figure out how long the char array should
     * be to hold all digits, decimal points, and negative signs (if applicable).
     *
     * @return The number of "digits" in the data variable
     */
    uint8_t calc_num_digits()
    {
        uint8_t num_digits = 1;
        if (_data < 0)
        {
            num_digits++; // Add space for negative sign
        }
        uint16_t temp = abs(_data);
        uint8_t num_decimals = _is_metric ? _num_decimals_metric : _num_decimals_imperial;
        if (num_decimals > 0)
        {
            num_digits++; // Add space for decimal point
            for (uint8_t i = 0; i < num_decimals; i++)
            {
                num_digits++; // Add space for each decimal place
            }
        }
        uint8_t num_whole_digits = int(log10(temp));
        if (num_whole_digits > 0)
        {
            for (int i = 0; i < num_whole_digits + 1; i++)
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

    /**
     * Sets the unit label of the data. This will change based on whether that Information
     * object is using Metric or Imperial data formatting, as well as the Unit Type of the
     * data.
     *
     * @note Format: [label]
     */
    void set_unit_label()
    {
        Unit_Type type = _unit_type;
        switch (type)
        {
        case TEMP:
            _unit_label = _is_metric ? "[°C]" : "[°F]";
            break;

        case PRESSURE:
            _unit_label = _is_metric ? "[bar]" : "[psi]";
            break;

        case SPEED:
            _unit_label = _is_metric ? "[kph]" : "[mph]";
            break;

        case TORQUE:
            _unit_label = _is_metric ? "[Nm]" : "[ft/lbs]";
            break;

        case VOLUME:
            _unit_label = _is_metric ? "[L]" : "[gal]";
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

public:
    /********************
     * Public Variables *
     ********************/

    /********************
     * Public Functions *
     ********************/

    /**
     * Constructor to fully define a new Information object. This will default the Unit_Region
     * to Metric and the data held within the Information object to 0.
     *
     * @param name An array of characters for the name of the Information object.
     * @param unit_type The type of data that the Information object holds. Type of Unit_Type.
     * @param num_decimals_metric The number of decimal places for Metric data format.
     * @param num_decimals_imperial The number of decimal places for Imperial data format.
     * @param refresh_rate The cycle time of the Information object in ms. Smaller is faster.
     */
    Information(Request **requests, uint8_t num_requests, String name, Unit_Type unit_type, uint16_t refresh_rate)
        : _requests(requests),
          _num_requests(num_requests),
          _refresh_rate(refresh_rate),
          _data(0),
          _name(name),
          _unit_type(unit_type)
    {
        set_unit_type(unit_type);
    }

    /**
     * Gets the name of the Information object.
     *
     * @return A String
     */
    String get_name()
    {
        return _name;
    }
    /**
     * Gets the name with the data label appended onto it as a char array.
     *
     * @return A String.
     *
     * @note Format: {Name} {[Unit Label]}
     */
    String get_name_and_label()
    {
        return _name + " " + _unit_label;
    }

    /**
     * Sets the data value of the Information object.
     *
     * @param data The new data value as a float.
     */
    void set_data(float data)
    {
        _data = data;
    }
    /**
     * Gets the data as a float.
     *
     * @return A float.
     */
    float get_data_num()
    {
        return _data;
    }
    /**
     * Gets the data as a string.
     *
     * @return A String.
     */
    String get_data_str()
    {
        return String(_data, _is_metric ? _num_decimals_metric : _num_decimals_imperial);
    }

    /**
     * Sets the Unit Type of the Information object.
     *
     * @param type The new Unit Type as a Unit_Type enum.
     */
    void set_unit_type(Unit_Type type)
    {
        _unit_type = type;
        set_unit_label();
        switch (type)
        {
        case LAMBDA:
            set_num_decimals_metric(1);
            set_num_decimals_imperial(1);
            break;

        case PRESSURE:
            set_num_decimals_metric(2);
            set_num_decimals_imperial(1);
            break;

        case VOLUME:
            set_num_decimals_metric(1);
            set_num_decimals_imperial(1);
            break;

        case DEGREES:
        case PERCENT:
        case RPM:
        case SPEED:
        case SUM:
        case TEMP:
        case TORQUE:
        case NONE:
        default:
            set_num_decimals_metric(0);
            set_num_decimals_imperial(0);
            break;
        }
    }
    /**
     * Gets the Unit Type of the Information object.
     *
     * @return The new Unit Type as a Unit_Type enum.
     */
    Unit_Type get_unit_type()
    {
        return _unit_type;
    }

    /**
     * Sets the number of decimal places to use when using Metric data.
     *
     * @param decimals The number of decimal places.
     */
    void set_num_decimals_metric(uint8_t decimals)
    {
        _num_decimals_metric = (decimals > 2) ? 2 : decimals;
    }
    /**
     * Gets the number of decimal places to use when using Metric data.
     *
     * @return The number of decimal places.
     */
    uint8_t get_num_decimals_metric()
    {
        return _num_decimals_metric;
    }

    /**
     * Sets the number of decimal places to use when using Imperial data.
     *
     * @param decimals The number of decimal places.
     */
    void set_num_decimals_imperial(uint8_t decimals)
    {
        _num_decimals_imperial = (decimals > 2) ? 2 : decimals;
    }
    /**
     * Gets the number of decimal places to use when using Imperial data.
     *
     * @return The number of decimal places.
     */
    uint8_t get_num_decimals_imperial()
    {
        return _num_decimals_imperial;
    }

    /**
     * Gets the refresh rate of the data coming in (in ms).
     *
     * @return The refresh rate in ms.
     */
    uint16_t get_refresh_rate()
    {
        return _refresh_rate;
    }

    /**
     * Sets the Unit Region of the Information object.
     *
     * @param is_metric Flag for whether Info is metric or not.
     */
    void set_is_metric(bool is_metric)
    {
        _is_metric = is_metric;
        set_unit_label();
    }
    /**
     * Gets the Unit Region of the Information object.
     *
     * @return True if Information is metric, false if imperial.
     */
    bool get_is_metric()
    {
        return _is_metric;
    }
};