/**
 * @file vehicle_binding.hpp
 * @brief Bridge between core:: and gui/widgets. Translates configuration data
 * into panels, and callbacks back to configuration data.
 */
#pragma once

#include "config_store.hpp"
#include "panel_widget.hpp"
#include "settings_screen.hpp"
#include "signal_store.hpp"
#include "warning_config_screen.hpp"

namespace gui
{

    /**
     * The core::DisplayMode <-> gui::DisplayMode translation. gui/bindings is
     * the declared core::/gui:: boundary (see the file doc comment), so these
     * live here rather than being re-implemented by each caller.
     */
    DisplayMode       to_gui_display_mode(core::DisplayMode mode);
    core::DisplayMode to_core_display_mode(DisplayMode mode);

    /**
     * Likewise core::BrightnessMode <-> gui::BrightnessMode, for
     * SettingsScreen's mode toggle.
     */
    BrightnessMode       to_gui_brightness_mode(core::BrightnessMode mode);
    core::BrightnessMode to_core_brightness_mode(BrightnessMode mode);

    class VehicleBinding
    {
        public:
            VehicleBinding(core::SignalStore &signals,
                           core::ConfigStore &config);

            /**
             * Builds rank's view-model from whatever signal it's currently
             * assigned to.
             */
            PanelViewModel build_view_model(uint8_t rank) const;

            /**
             * Reassigns rank to a different signal/unit system/display
             * mode, persisted immediately.
             */
            void apply_configurator_change(uint8_t          rank,
                                           uint16_t         new_signal_id,
                                           core::UnitSystem units,
                                           DisplayMode      mode);

            /**
             * Reads signal_id's current warning thresholds, for opening
             * WarningConfigScreen.
             */
            WarningThresholdViewModel read_warning_thresholds(
                uint16_t signal_id, bool is_metric) const;

            /**
             * Updates signal_id's warning thresholds.
             */
            void apply_warning_change(uint16_t signal_id,
                                      const WarningThresholdViewModel &vm);

            /**
             * Shows `values` on every panel in place of the live store, or
             * the live store again for nullptr. Used for phone configurator's
             * "live preview".
             */
            void set_value_override(const core::SignalStore *values)
            {
                override_ = values;
            }

        private:
            core::SignalStore       &signals_;
            const core::SignalStore *override_ = nullptr;
            core::ConfigStore       &config_;
    };

} // namespace gui
