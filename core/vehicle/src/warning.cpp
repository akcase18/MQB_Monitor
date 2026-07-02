#include "warning.hpp"

namespace core
{

WarningState evaluate_warning(const SignalValue       &value,
                               const WarningThresholds &thresholds)
{
    WarningState state{ false, false };

    if (!value.has_data)
    {
        return state;
    }

    state.lo_active = thresholds.use_lo && value.value <= thresholds.lo;
    state.hi_active = thresholds.use_hi && value.value >= thresholds.hi;

    return state;
}

} // namespace core
