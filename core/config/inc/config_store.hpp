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
            /* v2: added PanelConfig::display_mode. v3: added
             * Settings::accent_color. v4: added SignalConfig::
             * use_custom_range/custom_min/custom_max. v5: Config.panels[]
             * (screen-slot-indexed) renamed to Config.priorities[]
             * (priority-rank-indexed) — see config.hpp's file doc comment.
             * v6: added Settings::logging_enabled; the first version
             * dashes in the field hold. v7: Config.signals[] became a
             * fixed table of id-tagged records instead of one positional
             * record per catalog entry. v8: added Settings::
             * brightness_mode. v9: added Settings::auto_brightness_pct,
             * growing Settings by 4 bytes. v10: added Settings::
             * log_source (in padding) and the log profile after signals[].
             * migrate() carries v6-v9 across; anything older resets to
             * defaults. */
            static constexpr uint8_t CURRENT_VERSION = 10;

            explicit ConfigStore(IConfigStore &backing);

            Config &get();

            /**
             * Loads from backing storage. Unrecognized magic -> defaults
             * (and persists them). Older version -> migrate() (and persists
             * the result). Current version -> full load, then the signal
             * records are put back in catalog order, and saved if that
             * moved anything (a signal promoted, retired or reordered
             * since the last boot). Newer version --
             * written by firmware this board has since rolled back from
             * (an OTA update, see platform/dash_s3/OTA_PLAN.md) -> defaults,
             * NOT persisted: a layout this build doesn't know can't be
             * read safely, but leaving it in place means reinstalling the
             * newer firmware finds it again, until something here saves.
             * The first save after that is always a full one: see
             * save_priority().
             */
            void load();

            /** Writes the full config back to storage. */
            void save();

            /**
             * Writes only `priorities[rank]` back to storage -- or, if load()
             * left a newer firmware's config in place, the whole config via
             * save(). A partial write there would land at this layout's
             * offset inside the newer one, under a header that still names
             * the newer version, and that firmware would load it as valid.
             */
            void save_priority(uint8_t rank);

            /**
             * Writes only the given signal's SignalConfig back to storage,
             * with the same full-save fallback as save_priority().
             */
            void save_signal(uint16_t signal_id);

        private:
            void set_defaults();
            void migrate(uint8_t old_version);
            bool sort_signals();
            /**
             * Puts a rank whose signal this build doesn't offer (demoted to
             * WIP or internal, or removed) back to that rank's default.
             * Left alone, the panel shows nothing, the phone page can't
             * select it, and the dash rejects the phone's Save. True if
             * anything changed.
             */
            bool fix_priorities();
            /**
             * Drops log profile ids this build doesn't offer, and repeats,
             * keeping the rest in order. True if anything changed.
             */
            bool fix_log_profile();

            IConfigStore &backing_;
            Config        config_ {};
            /* Storage still holds a newer firmware's config (see load()). */
            bool          stored_is_newer_ = false;
    };

} // namespace core
