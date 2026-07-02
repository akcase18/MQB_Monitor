/**
 * @file warning.hpp
 * @brief Signal-scoped warning threshold evaluation. Lives in core/vehicle
 * (not core/config) so this stays ignorant of config's existence — config
 * just stores a WarningThresholds per signal and hands it here.
 */
#pragma once

#include "signal_store.hpp"

namespace core
{

    struct WarningThresholds
    {
            float lo;
            float hi;
            bool  use_lo;
            bool  use_hi;
    };

    struct WarningState
    {
            bool lo_active;
            bool hi_active;
    };

    /**
     * Compares `value` against `thresholds`, gated by each bound's own toggle
     * and by whether `value` has ever received data (ported from the old
     * Info::low_warning_active()/high_warning_active(), plus the has_data
     * guard so a signal can't false-trigger a warning before its first UDS
     * response).
     */
    WarningState evaluate_warning(const SignalValue       &value,
                                  const WarningThresholds &thresholds);

} // namespace core
