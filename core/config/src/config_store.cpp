#include "config_store.hpp"

#include <stddef.h>

#include "mem_placement.hpp"

namespace core
{

    /* flipped_horizontal/flipped_vertical and open_orientation_on_boot
     * were added into padding without a version bump (see config.hpp);
     * this is what makes that safe. */
    static_assert(offsetof(Settings, accent_color) == 12,
                  "Settings layout moved -- bump CURRENT_VERSION");

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

    FLASHMEM void ConfigStore::load()
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

        if (header.version > CURRENT_VERSION)
        {
            set_defaults();
            return;
        }

        backing_.read(0, &config_, sizeof(config_));
    }

    FLASHMEM void ConfigStore::save()
    {
        config_.header.magic   = MAGIC_NUMBER;
        config_.header.version = CURRENT_VERSION;
        backing_.write(0, &config_, sizeof(config_));
    }

    FLASHMEM void ConfigStore::save_priority(uint8_t rank)
    {
        if (rank >= MAX_PANELS)
        {
            return;
        }

        backing_.write(priority_offset(rank),
                       &config_.priorities[rank],
                       sizeof(PrioritizedSignal));
    }

    FLASHMEM void ConfigStore::save_signal(uint16_t signal_id)
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

    FLASHMEM void ConfigStore::set_defaults()
    {
        config_.header.magic   = MAGIC_NUMBER;
        config_.header.version = CURRENT_VERSION;

        config_.settings.day_brightness   = 100;
        config_.settings.night_brightness = 20;
        config_.settings.num_panels       = 5;
        config_.settings.orientation      = LayoutOrientation::Horizontal;
        config_.settings.logging_enabled  = false; /* opt-in, see config.hpp */
        config_.settings.flipped_horizontal = false;
        config_.settings.flipped_vertical   = false;
        config_.settings.open_orientation_on_boot = false;
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

        /* Whole SignalConfig, not just .warn -- this has to leave every
         * field at its default rather than relying on config_ never
         * having been loaded into, so migrate() can call it after a real
         * field-by-field migration lands. */
        for (size_t i = 0; i < SIGNAL_CATALOG_COUNT; i++)
        {
            config_.signals[i] = SignalConfig {};
        }
    }

    FLASHMEM void ConfigStore::migrate(uint8_t old_version)
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
