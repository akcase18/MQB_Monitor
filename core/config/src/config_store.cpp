#include "config_store.hpp"

#include <stddef.h>

#include "mem_placement.hpp"

namespace core
{

    /* flipped_horizontal/flipped_vertical and open_orientation_on_boot
     * were added into padding without a version bump (see config.hpp);
     * this is what makes that safe. */
    static_assert(offsetof(Settings, brightness_mode) == 3
                      && offsetof(Settings, accent_color) == 12
                      && offsetof(Settings, auto_brightness_pct) == 16,
                  "Settings layout moved -- bump CURRENT_VERSION");

    /* The stored layout for CURRENT_VERSION. Dashes in the field hold
     * real settings now, so changing it -- a field added, moved or
     * resized, or SIGNAL_CONFIG_SLOTS raised (promoting a signal no
     * longer changes it) -- needs a CURRENT_VERSION bump and a migrate() step that carries the
     * old values across. Without the step, load() would reset everyone's
     * settings; without the bump, it would misread them. Update these
     * numbers with it. */
    static_assert(ConfigStore::CURRENT_VERSION == 9 && sizeof(Config) == 1888
                      && offsetof(Config, settings) == 4
                      && offsetof(Config, priorities) == 24
                      && offsetof(Config, signals) == 96
                      && sizeof(SignalConfig) == 28
                      && offsetof(SignalConfig, signal_id) == 24,
                  "Config's stored layout changed -- bump CURRENT_VERSION "
                  "and add a migrate() step for the old one");

    /**
     * Esp32ConfigStore and the simulator's store are 4096 bytes, the
     * smallest of the platforms (the Teensy's EEPROM is 4284).
     */
    static_assert(sizeof(Config) <= 4096,
                  "Config no longer fits the smallest IConfigStore");

    namespace
    {
        /**
         * The v6-v8 layout: the same header, then a 16-byte Settings (the
         * current one less auto_brightness_pct), the same priorities, and
         * the signal records from byte 92. v6's records are one 24-byte
         * SignalConfig (no signal_id) per catalog entry, in the catalog
         * order of every build that wrote v6; v7's and v8's are the
         * current SIGNAL_CONFIG_SLOTS id-tagged ones.
         */
        constexpr size_t   OLD_SETTINGS_SIZE     = 16;
        constexpr size_t   OLD_PRIORITIES_OFFSET = 20;
        constexpr size_t   OLD_SIGNALS_OFFSET    = 92;
        constexpr size_t   V6_SIGNAL_CONFIG_SIZE = 24;
        constexpr uint16_t V6_SIGNAL_IDS[]       = {
            0,  1,  2,  3,  4,  5,  22, 7,  8,  9,  10,
            11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
        };

        static_assert(offsetof(Settings, auto_brightness_pct)
                              == OLD_SETTINGS_SIZE
                          && offsetof(SignalConfig, signal_id)
                                 == V6_SIGNAL_CONFIG_SIZE,
                      "migrate() reads the old settings and v6 records "
                      "straight into the current ones, which relies on "
                      "these lining up");

        /**
         * First slot at or after `from` holding `signal_id`, or -1.
         */
        int find_slot(const SignalConfig *slots, uint16_t signal_id, size_t from)
        {
            for (size_t s = from; s < SIGNAL_CONFIG_SLOTS; s++)
            {
                if (slots[s].signal_id == signal_id)
                {
                    return static_cast<int>(s);
                }
            }
            return -1;
        }

        /**
         * First slot at or after `from` that can be given up: an empty one,
         * or failing that one holding a signal this build doesn't have.
         * -1 if every slot from there on holds a catalog signal.
         */
        int find_spare_slot(const SignalConfig *slots, size_t from)
        {
            int empty = find_slot(slots, NO_SIGNAL_ID, from);
            if (empty >= 0)
            {
                return empty;
            }
            for (size_t s = from; s < SIGNAL_CONFIG_SLOTS; s++)
            {
                if (signal_catalog_find(slots[s].signal_id) < 0)
                {
                    return static_cast<int>(s);
                }
            }
            return -1;
        }

        /**
         * Ported from the old EEPROM_Handler::set_defaults(). info_list's
         * declaration order there matches SIGNAL_CATALOG's id assignment
         * here 1:1, so the same positions carry over directly, now read as
         * priority order (rank 0 = most important) rather than slot order
         * -- Coolant Temp lands in the most prominent slot at every panel
         * count instead of just being "whatever's in slot 0". priorities[5]
         * is a real, simply-inactive rank since num_panels defaults to 5.
         */
        constexpr PrioritizedSignal DEFAULT_PRIORITIES[MAX_PANELS] PROGMEM = {
            { 0, UnitSystem::Imperial },  /* Coolant Temp */
            { 12, UnitSystem::Imperial }, /* Throttle Pos */
            { 2, UnitSystem::Imperial },  /* Int Air Temp */
            { 1, UnitSystem::Imperial },  /* Eng Oil Temp */
            { 22, UnitSystem::Imperial }, /* Int Pres Act. */
            { 8, UnitSystem::Imperial },  /* Engine Torque */
        };

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
        stored_is_newer_ = false;

        if (header.magic != MAGIC_NUMBER)
        {
            set_defaults();
            save();
            return;
        }

        if (header.version < CURRENT_VERSION)
        {
            migrate(header.version);
            fix_priorities();
            save();
            return;
        }

        if (header.version > CURRENT_VERSION)
        {
            set_defaults();
            stored_is_newer_ = true;
            return;
        }

        backing_.read(0, &config_, sizeof(config_));
        bool fixed = false;
        if (config_.settings.brightness_mode > BrightnessMode::Auto)
        {
            config_.settings.brightness_mode = BrightnessMode::Day;
            fixed                            = true;
        }
        if (config_.settings.auto_brightness_pct > 100)
        {
            config_.settings.auto_brightness_pct = AUTO_BRIGHTNESS_NEUTRAL;
            fixed                                = true;
        }
        fixed = fix_priorities() || fixed;
        if (sort_signals() || fixed)
        {
            save();
        }
    }

    FLASHMEM void ConfigStore::save()
    {
        config_.header.magic   = MAGIC_NUMBER;
        config_.header.version = CURRENT_VERSION;
        backing_.write(0, &config_, sizeof(config_));
        stored_is_newer_ = false;
    }

    FLASHMEM void ConfigStore::save_priority(uint8_t rank)
    {
        if (rank >= MAX_PANELS)
        {
            return;
        }

        if (stored_is_newer_)
        {
            save();
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

        if (stored_is_newer_)
        {
            save();
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
        config_.settings.brightness_mode  = BrightnessMode::Day;
        config_.settings.auto_brightness_pct = AUTO_BRIGHTNESS_NEUTRAL;
        config_.settings.num_panels       = 5;
        config_.settings.orientation      = LayoutOrientation::Horizontal;
        config_.settings.logging_enabled  = false; /* opt-in, see config.hpp */
        config_.settings.flipped_horizontal = false;
        config_.settings.flipped_vertical   = false;
        config_.settings.open_orientation_on_boot = false;
        config_.settings.accent_color
            = 0x4CAF50; /* green, gui::ACCENT_PRESETS[0] */

        for (uint8_t rank = 0; rank < MAX_PANELS; rank++)
        {
            config_.priorities[rank] = DEFAULT_PRIORITIES[rank];
        }

        /* Whole SignalConfig, not just .warn -- this has to leave every
         * field at its default rather than relying on config_ never
         * having been loaded into, so migrate() can call it before
         * reading the old records in. */
        for (size_t s = 0; s < SIGNAL_CONFIG_SLOTS; s++)
        {
            config_.signals[s] = SignalConfig {};
        }
        sort_signals();
    }

    FLASHMEM void ConfigStore::migrate(uint8_t old_version)
    {
        set_defaults();

        /**
         * Versions before 6 never left the bench, so they just reset to
         * defaults -- matching the old EEPROM_Handler's fallback for
         * unknown versions. From 6 on, each version gets a step here that
         * converts its layout; the static_assert at the top of this file
         * is what forces the issue.
         */
        if (old_version < 6 || old_version > 8)
        {
            return;
        }

        /**
         * v6-v8 share the header, settings and priorities, all at the same
         * offsets (see OLD_SETTINGS_SIZE). Their settings are a prefix of
         * the current ones, so auto_brightness_pct keeps its default.
         * Before v8, brightness_mode's byte was padding and could hold
         * anything: Day is the level those dashes came up at, once boot
         * stopped forcing 100%.
         */
        backing_.read(offsetof(Config, settings),
                      &config_.settings,
                      OLD_SETTINGS_SIZE);
        backing_.read(OLD_PRIORITIES_OFFSET,
                      config_.priorities,
                      sizeof(config_.priorities));
        if (old_version < 8
            || config_.settings.brightness_mode > BrightnessMode::Auto)
        {
            config_.settings.brightness_mode = BrightnessMode::Day;
        }

        if (old_version >= 7)
        {
            backing_.read(OLD_SIGNALS_OFFSET,
                          config_.signals,
                          sizeof(config_.signals));
        }
        else
        {
            /**
             * Each v6 record is the first 24 bytes of a current one, so it
             * reads straight in and only needs its id.
             */
            constexpr size_t v6_count
                = sizeof(V6_SIGNAL_IDS) / sizeof(V6_SIGNAL_IDS[0]);
            for (size_t i = 0; i < v6_count; i++)
            {
                backing_.read(OLD_SIGNALS_OFFSET + i * V6_SIGNAL_CONFIG_SIZE,
                              &config_.signals[i],
                              V6_SIGNAL_CONFIG_SIZE);
                config_.signals[i].signal_id = V6_SIGNAL_IDS[i];
            }
        }

        /**
         * Moves the records to wherever this build's catalog wants them,
         * and gives any signal promoted since a fresh record.
         */
        sort_signals();
    }

    FLASHMEM bool ConfigStore::fix_priorities()
    {
        bool changed = false;
        for (uint8_t rank = 0; rank < MAX_PANELS; rank++)
        {
            if (signal_catalog_find(config_.priorities[rank].signal_id) >= 0)
            {
                continue;
            }
            config_.priorities[rank] = DEFAULT_PRIORITIES[rank];
            if (signal_catalog_find(config_.priorities[rank].signal_id) < 0)
            {
                config_.priorities[rank].signal_id = SIGNAL_CATALOG[0].id;
            }
            changed = true;
        }
        return changed;
    }

    FLASHMEM bool ConfigStore::sort_signals()
    {
        SignalConfig *slots   = config_.signals;
        bool          changed = false;

        for (size_t i = 0; i < SIGNAL_CATALOG_COUNT; i++)
        {
            uint16_t id    = SIGNAL_CATALOG[i].id;
            int      found = find_slot(slots, id, i);
            if (found == static_cast<int>(i))
            {
                continue;
            }
            changed = true;

            if (found >= 0)
            {
                SignalConfig held = slots[i];
                slots[i]          = slots[found];
                slots[found]      = held;
                continue;
            }

            /**
             * Nothing stored for this signal: it was promoted since the
             * last save. Whatever sits in slot i still matters (a catalog
             * signal further down, or a retired one worth keeping), so it
             * moves to a spare slot first. There is always one when it's a
             * catalog signal: fewer of those remain than slots from here
             * on. A retired one is dropped if the table is full of them.
             */
            if (slots[i].signal_id != NO_SIGNAL_ID)
            {
                int spare = find_spare_slot(slots, i + 1);
                if (spare >= 0)
                {
                    slots[spare] = slots[i];
                }
            }
            slots[i]           = SignalConfig {};
            slots[i].signal_id = id;
        }

        /**
         * Past the catalog only retired signals belong. A catalog id here
         * is a stray duplicate of a record already placed above.
         */
        for (size_t s = SIGNAL_CATALOG_COUNT; s < SIGNAL_CONFIG_SLOTS; s++)
        {
            if (slots[s].signal_id != NO_SIGNAL_ID
                && signal_catalog_find(slots[s].signal_id) >= 0)
            {
                slots[s] = SignalConfig {};
                changed  = true;
            }
        }

        return changed;
    }

} // namespace core
