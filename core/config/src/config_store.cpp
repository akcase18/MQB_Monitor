#include "config_store.hpp"

#include <stddef.h>

namespace core
{

namespace
{
size_t panel_offset(uint8_t idx)
{
    return offsetof(Config, panels)
         + static_cast<size_t>(idx) * sizeof(PanelConfig);
}

size_t signal_offset(size_t idx)
{
    return offsetof(Config, signals) + idx * sizeof(SignalConfig);
}
} // namespace

ConfigStore::ConfigStore(IConfigStore &backing) : backing_(backing)
{
}

Config &ConfigStore::get()
{
    return config_;
}

void ConfigStore::load()
{
    ConfigHeader header{};
    backing_.read(0, &header, sizeof(header));

    if (header.magic != MAGIC_NUMBER)
    {
        set_defaults();
        save();
        return;
    }

    if (header.version < CURRENT_VERSION)
    {
        migrate(header.version);
        save();
        return;
    }

    backing_.read(0, &config_, sizeof(config_));
}

void ConfigStore::save()
{
    config_.header.magic   = MAGIC_NUMBER;
    config_.header.version = CURRENT_VERSION;
    backing_.write(0, &config_, sizeof(config_));
}

void ConfigStore::save_panel(uint8_t idx)
{
    if (idx >= MAX_PANELS)
    {
        return;
    }

    backing_.write(
        panel_offset(idx), &config_.panels[idx], sizeof(PanelConfig));
}

void ConfigStore::save_signal(uint16_t signal_id)
{
    int idx = signal_catalog_find(signal_id);
    if (idx < 0)
    {
        return;
    }

    backing_.write(signal_offset(static_cast<size_t>(idx)),
                    &config_.signals[idx],
                    sizeof(SignalConfig));
}

void ConfigStore::set_defaults()
{
    config_.header.magic   = MAGIC_NUMBER;
    config_.header.version = CURRENT_VERSION;

    config_.settings.day_brightness   = 100;
    config_.settings.night_brightness = 20;
    config_.settings.num_panels       = 5;
    config_.settings.orientation      = LayoutOrientation::Horizontal;

    /* Ported from the old EEPROM_Handler::set_defaults(). info_list's
     * declaration order there matches SIGNAL_CATALOG's id assignment here
     * 1:1, so the same positions carry over directly. panels[5] was dead
     * (old NUM_PANELS=5 never wired panel_6) — it's now a real, simply
     * inactive slot since num_panels defaults to 5. */
    config_.panels[0] = { 0, UnitSystem::Imperial };  /* Coolant Temp */
    config_.panels[1] = { 12, UnitSystem::Imperial }; /* Throttle Pos */
    config_.panels[2] = { 2, UnitSystem::Imperial };  /* Int Air Temp */
    config_.panels[3] = { 1, UnitSystem::Imperial };  /* Eng Oil Temp */
    config_.panels[4] = { 6, UnitSystem::Imperial };  /* Int Pres Act. */
    config_.panels[5] = { 8, UnitSystem::Imperial };  /* Engine Torque */

    for (size_t i = 0; i < SIGNAL_CATALOG_COUNT; i++)
    {
        config_.signals[i].warn = { 0.0f, 0.0f, false, false };
    }
}

void ConfigStore::migrate(uint8_t old_version)
{
    /* CURRENT_VERSION is 1 — the first version of this schema, so there's
     * nothing older to convert from yet. Any unrecognized version resets
     * to defaults, matching the old EEPROM_Handler's fallback for unknown
     * versions. Future version bumps add real field-by-field migration
     * here, keyed on old_version. */
    (void)old_version;
    set_defaults();
}

} // namespace core
