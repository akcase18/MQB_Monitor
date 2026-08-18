#include "config_store.hpp"

#include <stddef.h>

namespace core
{

    namespace
    {
        size_t priority_offset(uint8_t rank)
        {
            return offsetof(Config, priorities)
                   + static_cast<size_t>(rank) * sizeof(PrioritizedSignal);
        }

        size_t signal_offset(size_t idx)
        {
            return offsetof(Config, signals) + idx * sizeof(SignalConfig);
        }
    } // namespace

    ConfigStore::ConfigStore(IConfigStore &backing)
        : backing_(backing)
    {
    }

    Config &ConfigStore::get()
    {
        return config_;
    }

    void ConfigStore::load()
    {
        ConfigHeader header {};
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

    void ConfigStore::save_priority(uint8_t rank)
    {
        if (rank >= MAX_PANELS)
        {
            return;
        }

        backing_.write(priority_offset(rank),
                       &config_.priorities[rank],
                       sizeof(PrioritizedSignal));
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
        config_.settings.accent_color
            = 0x4CAF50; /* green, gui::ACCENT_PRESETS[0] */

        /* Ported from the old EEPROM_Handler::set_defaults(). info_list's
         * declaration order there matches SIGNAL_CATALOG's id assignment
         * here 1:1, so the same positions carry over directly, now read as
         * priority order (rank 0 = most important) rather than slot order
         * — Coolant Temp lands in the most prominent slot at every panel
         * count instead of just being "whatever's in slot 0". priorities[5]
         * is a real, simply-inactive rank since num_panels defaults to 5. */
        config_.priorities[0] = { 0, UnitSystem::Imperial };  /* Coolant Temp */
        config_.priorities[1] = { 12, UnitSystem::Imperial }; /* Throttle Pos */
        config_.priorities[2] = { 2, UnitSystem::Imperial };  /* Int Air Temp */
        config_.priorities[3] = { 1, UnitSystem::Imperial };  /* Eng Oil Temp */
        config_.priorities[4] = { 22, UnitSystem::Imperial }; /* Int Pres Act. */
        config_.priorities[5] = { 8, UnitSystem::Imperial }; /* Engine Torque */

        for (size_t i = 0; i < SIGNAL_CATALOG_COUNT; i++)
        {
            config_.signals[i].warn = { 0.0f, 0.0f, false, false };
        }
    }

    void ConfigStore::migrate(uint8_t old_version)
    {
        /* No device has shipped with saved config yet, so every version
         * bump so far just resets to defaults rather than converting old
         * fields — matching the old EEPROM_Handler's fallback for unknown
         * versions. Once something has actually shipped, this needs real
         * field-by-field migration keyed on old_version instead. */
        (void)old_version;
        set_defaults();
    }

} // namespace core
