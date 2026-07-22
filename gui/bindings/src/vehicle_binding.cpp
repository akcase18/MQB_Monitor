#include "vehicle_binding.hpp"

#include <stdio.h>

#include "warning.hpp"

namespace gui
{

    namespace
    {
        DisplayMode to_gui_display_mode(core::DisplayMode mode)
        {
            switch (mode)
            {
                case core::DisplayMode::Bar:
                    return DisplayMode::Bar;
                case core::DisplayMode::Gauge:
                    return DisplayMode::Gauge;
                case core::DisplayMode::Digital:
                default:
                    return DisplayMode::Digital;
            }
        }

        core::DisplayMode to_core_display_mode(DisplayMode mode)
        {
            switch (mode)
            {
                case DisplayMode::Bar:
                    return core::DisplayMode::Bar;
                case DisplayMode::Gauge:
                    return core::DisplayMode::Gauge;
                case DisplayMode::Digital:
                default:
                    return core::DisplayMode::Digital;
            }
        }
    } // namespace

    VehicleBinding::VehicleBinding(core::SignalStore &signals,
                                   core::ConfigStore &config)
        : signals_(signals)
        , config_(config)
    {
    }

    PanelViewModel VehicleBinding::build_view_model(uint8_t rank,
                                                    bool    large,
                                                    bool    hero) const
    {
        PanelViewModel vm {};
        vm.large = large;
        vm.hero  = hero;

        if (rank >= core::MAX_PANELS)
        {
            return vm;
        }

        const core::Config            &cfg   = config_.get();
        const core::PrioritizedSignal &panel = cfg.priorities[rank];

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

        vm.mode = to_gui_display_mode(panel.display_mode);
        if (vm.mode != DisplayMode::Digital && value.has_data)
        {
            const core::SignalConfig &sig_cfg = cfg.signals[catalog_idx];
            core::DisplayRange        range
                = core::resolve_display_range(spec,
                                              sig_cfg.use_custom_range,
                                              sig_cfg.custom_min,
                                              sig_cfg.custom_max);
            float span = range.max - range.min;
            float pct = (span > 0.0f) ? (value.value - range.min) / span : 0.0f;
            vm.range_pct = pct < 0.0f ? 0.0f : (pct > 1.0f ? 1.0f : pct);
        }

        return vm;
    }

    void VehicleBinding::apply_configurator_change(uint8_t  rank,
                                                   uint16_t new_signal_id,
                                                   core::UnitSystem units,
                                                   DisplayMode      mode)
    {
        if (rank >= core::MAX_PANELS)
        {
            return;
        }

        core::PrioritizedSignal &panel = config_.get().priorities[rank];
        panel.signal_id                = new_signal_id;
        panel.units                    = units;
        panel.display_mode             = to_core_display_mode(mode);
        config_.save_priority(rank);
    }

    WarningThresholdViewModel VehicleBinding::read_warning_thresholds(
        uint16_t signal_id, bool is_metric) const
    {
        WarningThresholdViewModel vm {};

        int catalog_idx = core::signal_catalog_find(signal_id);
        if (catalog_idx < 0)
        {
            return vm;
        }

        const core::SignalSpec   &spec    = core::SIGNAL_CATALOG[catalog_idx];
        const core::SignalConfig &sig_cfg = config_.get().signals[catalog_idx];
        const core::WarningThresholds &warn = sig_cfg.warn;
        vm.use_lo                           = warn.use_lo;
        vm.use_hi                           = warn.use_hi;

        core::DisplayRange range
            = core::resolve_display_range(spec,
                                          sig_cfg.use_custom_range,
                                          sig_cfg.custom_min,
                                          sig_cfg.custom_max);

        float lo        = warn.lo;
        float hi        = warn.hi;
        float min_value = range.min;
        float max_value = range.max;
        float step      = core::warning_step(spec.unit_type);

        if (!is_metric)
        {
            lo = core::unit_convert_to_imperial(spec.unit_type, lo);
            hi = core::unit_convert_to_imperial(spec.unit_type, hi);
            min_value
                = core::unit_convert_to_imperial(spec.unit_type, min_value);
            max_value
                = core::unit_convert_to_imperial(spec.unit_type, max_value);
            /* step is a delta, not an absolute value — only the
             * conversion's multiplicative scale applies, not Temp's +32
             * offset, so subtract off the zero-point's converted value. */
            step = core::unit_convert_to_imperial(spec.unit_type, step)
                   - core::unit_convert_to_imperial(spec.unit_type, 0.0f);
        }

        vm.lo        = lo;
        vm.hi        = hi;
        vm.min_value = min_value;
        vm.max_value = max_value;
        vm.step      = step;
        vm.decimals  = core::unit_decimals(spec.unit_type, is_metric);
        vm.is_metric = is_metric;
        snprintf(vm.name, sizeof(vm.name), "%s", spec.name);
        snprintf(vm.unit_label,
                 sizeof(vm.unit_label),
                 "%s",
                 core::unit_label(spec.unit_type, is_metric));
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

        const core::SignalSpec &spec = core::SIGNAL_CATALOG[catalog_idx];

        float lo = vm.lo;
        float hi = vm.hi;
        if (!vm.is_metric)
        {
            lo = core::unit_convert_to_metric(spec.unit_type, lo);
            hi = core::unit_convert_to_metric(spec.unit_type, hi);
        }

        core::WarningThresholds thresholds { lo, hi, vm.use_lo, vm.use_hi };
        config_.get().signals[catalog_idx].warn = thresholds;
        config_.save_signal(signal_id);
    }

} // namespace gui
