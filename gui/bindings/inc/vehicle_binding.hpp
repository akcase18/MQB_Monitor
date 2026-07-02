/**
 * @file vehicle_binding.hpp
 * @brief The sole bridge between core:: and gui/widgets: translates
 * core::SignalStore/core::ConfigStore data into PanelViewModels, and
 * routes gui/screens callbacks (panel reassignment, warning threshold
 * edits) back into core::ConfigStore persistence.
 */
#pragma once

#include "config_store.hpp"
#include "panel_widget.hpp"
#include "signal_store.hpp"
#include "warning_config_screen.hpp"

namespace gui
{

    class VehicleBinding
    {
        public:
            VehicleBinding(core::SignalStore &signals,
                           core::ConfigStore &config);

            /**
             * Builds panel_idx's view-model from whatever signal it's
             * currently assigned to. `large` is a layout decision (see
             * gui/screens/layout.hpp), passed through rather than derived
             * from vehicle data.
             */
            PanelViewModel build_view_model(uint8_t panel_idx,
                                            bool    large) const;

            /** Reassigns panel_idx to a different signal/unit system, persisted
             * immediately. */
            void apply_configurator_change(uint8_t          panel_idx,
                                           uint16_t         new_signal_id,
                                           core::UnitSystem units);

            /** Reads signal_id's current warning thresholds, for opening
             * WarningConfigScreen. */
            WarningThresholdViewModel read_warning_thresholds(
                uint16_t signal_id) const;

            /** Updates signal_id's warning thresholds, persisted immediately.
             */
            void apply_warning_change(uint16_t signal_id,
                                      const WarningThresholdViewModel &vm);

        private:
            core::SignalStore &signals_;
            core::ConfigStore &config_;
    };

} // namespace gui
