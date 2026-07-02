/**
 * @file config_store.hpp
 * @brief Loads/saves Config over IConfigStore (EEPROM on Teensy, a file on
 * the simulator). Ported from the old EEPROM_Handler: same magic-number
 * check, same version-gated migration shape, same partial-write-a-slot
 * pattern for panels — extended with an equivalent for signals, since
 * warning thresholds are now signal-keyed rather than panel-keyed.
 */
#pragma once

#include "config.hpp"
#include "platform.hpp"

namespace core
{

class ConfigStore
{
    public:
        static constexpr uint16_t MAGIC_NUMBER    = 0xFEA7;
        static constexpr uint8_t  CURRENT_VERSION = 1;

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

        /** Writes only `panels[idx]` back to storage. */
        void save_panel(uint8_t idx);

        /** Writes only the given signal's SignalConfig back to storage. */
        void save_signal(uint16_t signal_id);

    private:
        void set_defaults();
        void migrate(uint8_t old_version);

        IConfigStore &backing_;
        Config        config_{};
};

} // namespace core
