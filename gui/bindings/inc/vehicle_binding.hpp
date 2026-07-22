/**
 * @file vehicle_binding.hpp
 * @brief The sole bridge between core:: and gui/widgets: translates
 * core::SignalStore/core::ConfigStore data into PanelViewModels, and
 * routes gui/screens callbacks (panel reassignment, warning threshold
 * edits) back into core::ConfigStore persistence. `rank` throughout this
 * class means priority rank (Config.priorities[] index, 0 = most
 * important) — callers resolve which on-screen slot a rank occupies via
 * gui::compute_prominence_ranks(), not here.
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
             * Builds rank's view-model from whatever signal it's currently
             * assigned to. `large`/`hero` are layout decisions (see
             * gui/screens/layout.hpp), passed through rather than derived
             * from vehicle data.
             */
            PanelViewModel build_view_model(uint8_t rank,
                                            bool    large,
                                            bool    hero) const;

            /** Reassigns rank to a different signal/unit system/display
             * mode, persisted immediately. */
            void apply_configurator_change(uint8_t          rank,
                                           uint16_t         new_signal_id,
                                           core::UnitSystem units,
                                           DisplayMode      mode);

            /** Reads signal_id's current warning thresholds, for opening
             * WarningConfigScreen. `is_metric` is the unit system of the
             * panel that's opening the editor — lo/hi/min_value/max_value/
             * step come back already converted to that unit system, since
             * editing e.g. a Fahrenheit-displayed signal's threshold in its
             * native Celsius storage unit would show a confusingly wrong
             * number. */
            WarningThresholdViewModel read_warning_thresholds(
                uint16_t signal_id, bool is_metric) const;

            /** Updates signal_id's warning thresholds, persisted immediately.
             * `vm.lo`/`vm.hi` are converted from `vm.is_metric`'s unit
             * system back to the signal's native (metric) storage unit
             * before saving. */
            void apply_warning_change(uint16_t signal_id,
                                      const WarningThresholdViewModel &vm);

        private:
            core::SignalStore &signals_;
            core::ConfigStore &config_;
    };

} // namespace gui
