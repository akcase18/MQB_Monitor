#include "Arduino.h"
#include <vector>

using namespace std;

/***********
 * Defines *
 ***********/

#define MAX_REFRESH 20
#define MIN_REFRESH 5000
#define NO_OFFSET 0
#define NO_SCALE 1

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
    String get_unit_label();

public:
    /********************
     * Public Variables *
     ********************/

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
    Information(String name, uint16_t byte_1, uint16_t byte_2, int16_t offset, float scale, Unit_Type unit_type, uint8_t num_decimals_metric, uint8_t num_decimals_imperial, uint16_t refresh_rate);

    /**
     * Sets the name of the Information object.
     *
     * @param name A char array of the new name of the Information object.
     */
    void set_name(String name);
    /**
     * Gets the name of the Information object.
     *
     * @return A String
     */
    String get_name();
    /**
     * Gets the name with the data label appended onto it as a char array.
     *
     * @return A String.
     *
     * @note Format: {Name} {[Unit Label]}
     */
    String get_name_and_label();

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
     * @return A String.
     */
    String get_data_str();

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
    
    /**
     * Sets the Unit Region of the Information object.
     *
     * @param is_metric Flag for whether Info is metric or not.
     */
    void set_is_metric(bool is_metric);
    /**
     * Gets the Unit Region of the Information object.
     *
     * @return True if Information is metric, false if imperial.
     */
    bool get_is_metric();
};