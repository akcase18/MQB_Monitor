#include "vehicle_binding.hpp"

#include <stdio.h>

#include "mem_placement.hpp"
#include "warning.hpp"

namespace gui
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

    FLASHMEM BrightnessMode to_gui_brightness_mode(core::BrightnessMode mode)
    {
        switch (mode)
        {
            case core::BrightnessMode::Night:
                return BrightnessMode::Night;
            case core::BrightnessMode::Auto:
                return BrightnessMode::Auto;
            case core::BrightnessMode::Day:
            default:
                return BrightnessMode::Day;
        }
    }

    FLASHMEM core::BrightnessMode to_core_brightness_mode(BrightnessMode mode)
    {
        switch (mode)
        {
            case BrightnessMode::Night:
                return core::BrightnessMode::Night;
            case BrightnessMode::Auto:
                return core::BrightnessMode::Auto;
            case BrightnessMode::Day:
            default:
                return core::BrightnessMode::Day;
        }
    }

    namespace
    {
        void copy_str(char *dst, size_t cap, const char *src)
        {
            size_t i = 0;
            for (; i + 1 < cap && src[i] != '\0'; i++)
            {
                dst[i] = src[i];
            }
            dst[i] = '\0';
        }
    } // namespace

    VehicleBinding::VehicleBinding(core::SignalStore &signals,
                                   core::ConfigStore &config)
        : signals_(signals)
        , config_(config)
    {
    }

    PanelViewModel VehicleBinding::build_view_model(uint8_t rank) const
    {
        PanelViewModel vm {};

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
        bool is_state                = core::signal_is_state(spec);

        copy_str(vm.name, sizeof(vm.name), spec.name);
        copy_str(vm.unit_label,
                 sizeof(vm.unit_label),
                 core::unit_label(spec.unit_type, is_metric));

        /**
         * Keyed by index not Signal ID.
         */
        const core::SignalStore &store
            = override_ != nullptr ? *override_ : signals_;
        const core::SignalValue &value = store.get_at(catalog_idx);
        vm.is_text                     = is_state;
        if (value.has_data && is_state)
        {
            /**
             * A state shows its label. A value with no label yet shows as ?<n>
             * so it can still be read off and added.
             */
            const char *label = core::signal_enum_label(spec, value.value);
            if (label != nullptr)
            {
                copy_str(vm.value_str, sizeof(vm.value_str), label);
            }
            else
            {
                snprintf(vm.value_str,
                         sizeof(vm.value_str),
                         "?%ld",
                         static_cast<long>(value.value < 0.0f
                                               ? value.value - 0.5f
                                               : value.value + 0.5f));
            }
        }
        else if (value.has_data)
        {
            float display = store.get_display_value_at(catalog_idx, is_metric);
            snprintf(vm.value_str,
                     sizeof(vm.value_str),
                     "%.*f",
                     core::unit_decimals(spec.unit_type, is_metric),
                     display);
        }
        else
        {
            copy_str(vm.value_str, sizeof(vm.value_str), "--");
        }

        /**
         * A state has no thresholds, and only reads as a label.
         */
        if (!is_state)
        {
            core::WarningState warn
                = core::evaluate_warning(value, cfg.signals[catalog_idx].warn);
            vm.warn_lo_active = warn.lo_active;
            vm.warn_hi_active = warn.hi_active;
        }

        vm.mode = is_state ? DisplayMode::Digital
                           : to_gui_display_mode(panel.display_mode);
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

        int catalog_idx = core::signal_catalog_find(new_signal_id);
        if (catalog_idx >= 0
            && core::signal_is_state(core::SIGNAL_CATALOG[catalog_idx]))
        {
            mode = DisplayMode::Digital;
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
        copy_str(vm.name, sizeof(vm.name), spec.name);
        copy_str(vm.unit_label,
                 sizeof(vm.unit_label),
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
