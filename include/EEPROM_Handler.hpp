#ifndef EEPROM_HANDLER_HPP
#define EEPROM_HANDLER_HPP

#ifdef USE_ARDUINO
    #include <EEPROM.h>
#else
    #include <stdint.h>
#endif

/*==============================================================================
 Defines
 =============================================================================*/

#define MAX_NUM_PANELS 6
#define MAGIC_NUMBER   0xFEA7

enum UnitSystem
{
    IMPERIAL,
    METRIC
};

struct Config_Header
{
        uint16_t magic;
        uint8_t  version;
};

struct Panel_Config
{
        uint8_t    panel_info;
        UnitSystem units;
};

struct Settings_Config
{
        uint8_t day_brightness;
        uint8_t night_brightness;
};

struct Config
{
        Config_Header   header;
        Settings_Config settings;
        Panel_Config    panels[MAX_NUM_PANELS];
};

class EEPROM_Handler
{
    public:
        static constexpr uint8_t CURRENT_VERSION = 1;

        /* Public Variables */
        Config config;

        EEPROM_Handler();

        /**
         * Load the configuration from EEPROM
         */
        void load_config();

        /**
         * Save the current configuration to EEPROM
         */
        void save_config();

        /**
         * Load panel configuration from EEPROM
         *
         * @param panel_num the panel number (0-indexed)
         * @param panel_info reference to Panel_Config struct to load data into
         */
        void load_panel(uint8_t panel_num, Panel_Config &panel_info);

        /**
         * Save panel configuration to EEPROM
         *
         * @param panel_num the panel number (0-indexed)
         * @param panel_info reference to Panel_Config struct to save data from
         */
        void save_panel(uint8_t panel_num, const Panel_Config &panel_info);

        /**
         * Reset the configuration to defaults
         */
        void set_defaults();

    private:
        /**
         * Get the EEPROM address for a given panel index
         *
         * @param index the panel index (0-indexed)
         * @return the EEPROM address for the panel configuration
         */
        uint16_t panelAddress(uint8_t index);

        /**
         * Migrate old configuration versions to the current version
         *
         * @param oldVersion the version number of the old configuration
         */
        void migrate(uint8_t oldVersion);
};

extern EEPROM_Handler eeprom;

#endif