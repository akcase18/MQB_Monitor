#include "peak_hold.hpp"

#include "signal_catalog.hpp"

namespace core
{

    void PeakHold::reset()
    {
        for (size_t i = 0; i < SIGNAL_CATALOG_COUNT; i++)
        {
            entries_[i] = Entry {};
        }
    }

    void PeakHold::sample(const SignalStore &store)
    {
        for (size_t i = 0; i < SIGNAL_CATALOG_COUNT; i++)
        {
            const SignalValue &value = store.get_at(static_cast<int>(i));
            if (!value.has_data)
            {
                /**
                 * See the header: a source that declines to write must not
                 * contribute a peak. */
                continue;
            }

            Entry &entry = entries_[i];
            if (!entry.has_data)
            {
                entry.min      = value.value;
                entry.max      = value.value;
                entry.has_data = true;
                continue;
            }

            if (value.value < entry.min)
            {
                entry.min = value.value;
            }
            if (value.value > entry.max)
            {
                entry.max = value.value;
            }
        }
    }

    bool PeakHold::has_data_at(int idx) const
    {
        if (idx < 0 || idx >= static_cast<int>(SIGNAL_CATALOG_COUNT))
        {
            return false;
        }
        return entries_[idx].has_data;
    }

    float PeakHold::min_at(int idx) const
    {
        if (!has_data_at(idx))
        {
            return 0.0f;
        }
        return entries_[idx].min;
    }

    float PeakHold::max_at(int idx) const
    {
        if (!has_data_at(idx))
        {
            return 0.0f;
        }
        return entries_[idx].max;
    }

    bool PeakHold::has_data(uint16_t id) const
    {
        return has_data_at(signal_catalog_find(id));
    }

    float PeakHold::min(uint16_t id) const
    {
        return min_at(signal_catalog_find(id));
    }

    float PeakHold::max(uint16_t id) const
    {
        return max_at(signal_catalog_find(id));
    }

} // namespace core
