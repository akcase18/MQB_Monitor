#include "EEPROM_Handler.hpp"

/*==============================================================================
 EEPROM Handler Object
 =============================================================================*/

EEPROM_Handler eeprom;

/*==============================================================================
 Constructors
 =============================================================================*/

EEPROM_Handler::EEPROM_Handler()
{
}

/*==============================================================================
 Public Functions
 =============================================================================*/

void EEPROM_Handler::load_config()
{
    // Load header
    EEPROM.get(0, config.header);

    if (config.header.magic != MAGIC_NUMBER)
    {
        set_defaults();
        save_config();
        return;
    }
    else if (config.header.version < CURRENT_VERSION)
    {
        // Future proofing for config changes
        migrate(config.header.version);
        save_config();
        return;
    }
    else
    {
        // Load full config
        EEPROM.get(0, config);
    }
}

void EEPROM_Handler::save_config()
{
    config.header.magic   = MAGIC_NUMBER;
    config.header.version = CURRENT_VERSION;
    EEPROM.put(0, config);
}

void EEPROM_Handler::load_panel(uint8_t panel_num, Panel_Config &panel_info)
{
    uint16_t addr = panelAddress(panel_num);
    EEPROM.get(addr, panel_info);
}

void EEPROM_Handler::save_panel(uint8_t             panel_num,
                                const Panel_Config &panel_info)
{
    config.panels[panel_num] = panel_info;
    uint16_t addr            = panelAddress(panel_num);
    EEPROM.put(addr, panel_info);
}

void EEPROM_Handler::set_defaults()
{
    config.header.magic              = MAGIC_NUMBER;
    config.header.version            = CURRENT_VERSION;
    config.settings.day_brightness   = 100;
    config.settings.night_brightness = 20;

    // Panel defaults
    config.panels[0] = { 0, IMPERIAL };
    config.panels[1] = { 12, IMPERIAL };
    config.panels[2] = { 2, IMPERIAL };
    config.panels[3] = { 1, IMPERIAL };
    config.panels[4] = { 6, IMPERIAL };
    config.panels[5] = { 8, IMPERIAL };
}

/*==============================================================================
 Private Functions
 =============================================================================*/

uint16_t EEPROM_Handler::panelAddress(uint8_t index)
{
    return sizeof(Config_Header) + sizeof(Settings_Config)
           + (index * sizeof(Panel_Config));
}

void EEPROM_Handler::migrate(uint8_t oldVersion)
{
    // Step 1: load what exists in EEPROM
    if (oldVersion == 1)
    {
        struct OldConfigV1
        {
                Config_Header   header;
                Settings_Config settings;
                Panel_Config    panels[MAX_NUM_PANELS];
        } oldConfig;

        EEPROM.get(0, oldConfig);

        // Step 2: copy old fields into new config
        config.header.magic   = MAGIC_NUMBER;
        config.header.version = CURRENT_VERSION;
        config.settings       = oldConfig.settings;
        for (uint8_t i = 0; i < MAX_NUM_PANELS; i++)
        {
            config.panels[i] = oldConfig.panels[i];
        }

        // Step 3: initialize any new fields with defaults
        // (none in this case)
    }
    else
    {
        // If version is unknown → reset
        set_defaults();
    }
}