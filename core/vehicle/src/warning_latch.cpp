#include "warning_latch.hpp"

namespace core
{

    void WarningLatch::set_qualify_ms(uint16_t ms)
    {
        qualify_ms_ = ms;
    }

    void WarningLatch::reset()
    {
        for (size_t i = 0; i < SIGNAL_CATALOG_COUNT; i++)
        {
            entries_[i] = Entry {};
        }
    }

    WarningState WarningLatch::update(int                      idx,
                                      const SignalValue       &value,
                                      const WarningThresholds &thresholds,
                                      uint32_t                 now_ms)
    {
        WarningState live = evaluate_warning(value, thresholds);

        if (idx < 0 || idx >= static_cast<int>(SIGNAL_CATALOG_COUNT))
        {
            /**
             * Nowhere to latch it, so report the live state unchanged
             * rather than swallowing a warning entirely. */
            return live;
        }

        Entry &entry = entries_[idx];

        entry.lo_crossing = live.lo_active;
        entry.hi_crossing = live.hi_active;

        if (live.lo_active)
        {
            if (entry.lo_since_ms == 0)
            {
                /**
                 * +1 so the stamp is never 0, which is the "not currently
                 * crossing" sentinel and is also a legitimate now_ms for
                 * the first millisecond after boot. */
                entry.lo_since_ms = now_ms + 1;
            }
            if ((now_ms + 1 - entry.lo_since_ms) >= qualify_ms_)
            {
                entry.lo_held = true;
            }
        }
        else
        {
            entry.lo_since_ms = 0;
        }

        if (live.hi_active)
        {
            if (entry.hi_since_ms == 0)
            {
                entry.hi_since_ms = now_ms + 1;
            }
            if ((now_ms + 1 - entry.hi_since_ms) >= qualify_ms_)
            {
                entry.hi_held = true;
            }
        }
        else
        {
            entry.hi_since_ms = 0;
        }

        WarningState out;
        out.lo_active = entry.lo_held;
        out.hi_active = entry.hi_held;
        return out;
    }

    void WarningLatch::acknowledge()
    {
        for (size_t i = 0; i < SIGNAL_CATALOG_COUNT; i++)
        {
            acknowledge_at(static_cast<int>(i));
        }
    }

    void WarningLatch::acknowledge_at(int idx)
    {
        if (idx < 0 || idx >= static_cast<int>(SIGNAL_CATALOG_COUNT))
        {
            return;
        }

        Entry &entry  = entries_[idx];
        entry.lo_held = false;
        entry.hi_held = false;

        /**
         * Qualification restarts too, so a fault that is still present
         * re-raises after qualify_ms rather than instantly on the next
         * sample. Dismissing a notice should buy at least that much quiet. */
        entry.lo_since_ms = 0;
        entry.hi_since_ms = 0;
    }

    bool WarningLatch::held_at(int idx) const
    {
        if (idx < 0 || idx >= static_cast<int>(SIGNAL_CATALOG_COUNT))
        {
            return false;
        }
        return entries_[idx].lo_held || entries_[idx].hi_held;
    }

    WarningState WarningLatch::state_at(int idx) const
    {
        if (idx < 0 || idx >= static_cast<int>(SIGNAL_CATALOG_COUNT))
        {
            return WarningState { false, false };
        }
        return WarningState { entries_[idx].lo_held, entries_[idx].hi_held };
    }

    bool WarningLatch::any_held() const
    {
        for (size_t i = 0; i < SIGNAL_CATALOG_COUNT; i++)
        {
            if (held_at(static_cast<int>(i)))
            {
                return true;
            }
        }
        return false;
    }

    bool WarningLatch::is_stale_at(int idx) const
    {
        if (idx < 0 || idx >= static_cast<int>(SIGNAL_CATALOG_COUNT))
        {
            return false;
        }

        const Entry &entry    = entries_[idx];
        bool         held     = entry.lo_held || entry.hi_held;
        bool         crossing = entry.lo_crossing || entry.hi_crossing;
        return held && !crossing;
    }

} // namespace core
