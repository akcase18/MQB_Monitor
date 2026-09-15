#include "config_store.hpp"

#include <stddef.h>

#include "mem_placement.hpp"

namespace core
{

    /**
     * The product's factory panel assignment: DEFAULT_NUM_PANELS and
     * DEFAULT_PRIORITIES. Generated from that product's signals.json and
     * selected by include path -- see tools/gen_catalog.py. */
#include "signal_defaults.generated.inc"

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
        config_.settings.num_panels       = DEFAULT_NUM_PANELS;
        config_.settings.orientation      = LayoutOrientation::Horizontal;
        config_.settings.logging_enabled  = false; /* opt-in, see config.hpp */
        config_.settings.accent_color
            = 0x4CAF50; /* green, gui::ACCENT_PRESETS[0] */

        /**
         * The factory panel assignment comes from the product's registry,
         * not from ids written out here. It used to be six hardcoded MQB
         * catalog ids with hand-maintained name comments -- correct for
         * exactly one product, and silently wrong for any other, since an
         * id absent from the compiled catalog resolves to -1 and shows
         * nothing at all. See tools/gen_catalog.py. */
        for (uint8_t rank = 0; rank < MAX_PANELS; rank++)
        {
            config_.priorities[rank] = DEFAULT_PRIORITIES[rank];
        }

        /**
         * Whole SignalConfig, not just .warn -- this has to leave every
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
        /**
         * No device has shipped with saved config yet, so every version
         * bump so far just resets to defaults rather than converting old
         * fields — matching the old EEPROM_Handler's fallback for unknown
         * versions. Once something has actually shipped, this needs real
         * field-by-field migration keyed on old_version instead. */
        (void)old_version;
        set_defaults();
    }

} // namespace core
