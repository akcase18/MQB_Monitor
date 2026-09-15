/**
 * @file config_store.hpp
 * @brief Loads/saves Config over IConfigStore (EEPROM on Teensy, a file on
 * the simulator). Ported from the old EEPROM_Handler: same magic-number
 * check, same version-gated migration shape, same partial-write-a-slot
 * pattern for priorities — extended with an equivalent for signals, since
 * warning thresholds are signal-keyed rather than slot-keyed.
 */
#pragma once

#include "config.hpp"
#include "platform.hpp"

namespace core
{

    class ConfigStore
    {
        public:
            static constexpr uint16_t MAGIC_NUMBER = 0xFEA7;
            /**
             * v2: added PanelConfig::display_mode. v3: added
             * Settings::accent_color. v4: added SignalConfig::
             * use_custom_range/custom_min/custom_max. v5: Config.panels[]
             * (screen-slot-indexed) renamed to Config.priorities[]
             * (priority-rank-indexed) — see config.hpp's file doc comment.
             * No device has shipped with saved config yet, so migrate()
             * still just falls through to defaults rather than preserving
             * old fields. */
            static constexpr uint8_t CURRENT_VERSION = 6;

            explicit ConfigStore(IConfigStore &backing);

            Config &get();

            /**
             * Loads from backing storage. Unrecognized magic -> defaults
             * (and persists them). Older version -> migrate() (and persists
             * the result). Current version -> full load.
             */
            void load();

            /** Writes the full config back to storage. */
            void save();

            /** Writes only `priorities[rank]` back to storage. */
            void save_priority(uint8_t rank);

            /** Writes only the given signal's SignalConfig back to storage. */
            void save_signal(uint16_t signal_id);

        private:
            void set_defaults();
            void migrate(uint8_t old_version);

            IConfigStore &backing_;
            Config        config_ {};
    };

} // namespace core
