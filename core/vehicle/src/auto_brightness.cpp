#include "auto_brightness.hpp"

#include "mem_placement.hpp"

namespace core
{

    namespace
    {
        /** `pct` clamped to 0-100; NaN reads as 0. */
        float clamp_pct(float pct)
        {
            if (!(pct > 0.0f))
            {
                return 0.0f;
            }
            return pct > 100.0f ? 100.0f : pct;
        }
    } // namespace

    FLASHMEM float AutoBrightness::shift_for(uint8_t slider_pct)
    {
        float from_mid = static_cast<float>(slider_pct > 100 ? 100 : slider_pct)
                         - 50.0f;
        return from_mid * MAX_SHIFT_PCT / 50.0f;
    }

    FLASHMEM uint8_t AutoBrightness::level_for(float dim_pct, float shift_pct)
    {
        return static_cast<uint8_t>(
            clamp_pct(clamp_pct(dim_pct) + shift_pct) + 0.5f);
    }

    FLASHMEM void AutoBrightness::start(uint8_t pct, uint32_t now_ms)
    {
        level_   = pct > 100 ? 100.0f : static_cast<float>(pct);
        last_ms_ = now_ms;
    }

    FLASHMEM uint8_t AutoBrightness::update(const SignalValue &dim,
                                            float              shift_pct,
                                            uint8_t            fallback_pct,
                                            uint32_t           now_ms,
                                            bool               snap)
    {
        bool fresh = dim.has_data && now_ms - dim.last_update_ms <= STALE_MS;
        float target = static_cast<float>(
            fresh ? level_for(dim.value, shift_pct)
                  : (fallback_pct > 100 ? 100 : fallback_pct));

        float step = static_cast<float>(RAMP_PCT_PER_S)
                     * static_cast<float>(now_ms - last_ms_) / 1000.0f;
        last_ms_ = now_ms;

        if (snap)
        {
            level_ = target;
        }
        else if (level_ < target)
        {
            level_ = level_ + step < target ? level_ + step : target;
        }
        else if (level_ > target)
        {
            level_ = level_ - step > target ? level_ - step : target;
        }

        return static_cast<uint8_t>(level_ + 0.5f);
    }

} // namespace core
