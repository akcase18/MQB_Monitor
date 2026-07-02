#include "vehicle_binding.hpp"

#include <stdio.h>

#include "warning.hpp"

namespace gui
{

    VehicleBinding::VehicleBinding(core::SignalStore &signals,
                                   core::ConfigStore &config)
        : signals_(signals)
        , config_(config)
    {
    }

    PanelViewModel VehicleBinding::build_view_model(uint8_t panel_idx,
                                                    bool    large) const
    {
        PanelViewModel vm {};
        vm.large = large;

        if (panel_idx >= core::MAX_PANELS)
        {
            return vm;
        }

        const core::Config      &cfg   = config_.get();
        const core::PanelConfig &panel = cfg.panels[panel_idx];

        int catalog_idx = core::signal_catalog_find(panel.signal_id);
        if (catalog_idx < 0)
        {
            return vm;
        }

        const core::SignalSpec &spec = core::SIGNAL_CATALOG[catalog_idx];
        bool is_metric               = panel.units == core::UnitSystem::Metric;

        snprintf(vm.name, sizeof(vm.name), "%s", spec.name);
        snprintf(vm.unit_label,
                 sizeof(vm.unit_label),
                 "%s",
                 core::unit_label(spec.unit_type, is_metric));

        const core::SignalValue &value = signals_.get(spec.id);
        if (value.has_data)
        {
            float display = signals_.get_display_value(spec.id, is_metric);
            snprintf(vm.value_str,
                     sizeof(vm.value_str),
                     "%.*f",
                     core::unit_decimals(spec.unit_type, is_metric),
                     display);
        }
        else
        {
            snprintf(vm.value_str, sizeof(vm.value_str), "--");
        }

        core::WarningState warn
            = core::evaluate_warning(value, cfg.signals[catalog_idx].warn);
        vm.warn_lo_active = warn.lo_active;
        vm.warn_hi_active = warn.hi_active;

        return vm;
    }

    void VehicleBinding::apply_configurator_change(uint8_t  panel_idx,
                                                   uint16_t new_signal_id,
                                                   core::UnitSystem units)
    {
        if (panel_idx >= core::MAX_PANELS)
        {
            return;
        }

        config_.get().panels[panel_idx] = { new_signal_id, units };
        config_.save_panel(panel_idx);
    }

    WarningThresholdViewModel VehicleBinding::read_warning_thresholds(
        uint16_t signal_id) const
    {
        WarningThresholdViewModel vm {};

        int catalog_idx = core::signal_catalog_find(signal_id);
        if (catalog_idx < 0)
        {
            return vm;
        }

        const core::WarningThresholds &warn
            = config_.get().signals[catalog_idx].warn;
        vm.use_lo = warn.use_lo;
        vm.use_hi = warn.use_hi;
        vm.lo     = warn.lo;
        vm.hi     = warn.hi;
        return vm;
    }

    void VehicleBinding::apply_warning_change(
        uint16_t signal_id, const WarningThresholdViewModel &vm)
    {
        int catalog_idx = core::signal_catalog_find(signal_id);
        if (catalog_idx < 0)
        {
            return;
        }

        core::WarningThresholds thresholds {
            vm.lo, vm.hi, vm.use_lo, vm.use_hi
        };
        config_.get().signals[catalog_idx].warn = thresholds;
        config_.save_signal(signal_id);
    }

} // namespace gui
