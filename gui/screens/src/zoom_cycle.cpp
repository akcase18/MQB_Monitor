#include "zoom_cycle.hpp"

namespace gui
{

    void ZoomCycle::set_overview_count(uint8_t count)
    {
        if (count < 1)
        {
            count = 1;
        }
        else if (count > MAX_PANELS)
        {
            count = MAX_PANELS;
        }

        overview_count_ = count;
        position_       = 0;
    }

    void ZoomCycle::advance()
    {
        if (overview_count_ <= 1)
        {
            return;
        }

        position_++;
        if (position_ > overview_count_)
        {
            position_ = 0;
        }
    }

    void ZoomCycle::reset()
    {
        position_ = 0;
    }

    bool ZoomCycle::zoomed() const
    {
        return position_ != 0;
    }

    uint8_t ZoomCycle::zoomed_rank() const
    {
        return zoomed() ? static_cast<uint8_t>(position_ - 1) : 0;
    }

    uint8_t ZoomCycle::panel_count() const
    {
        return zoomed() ? 1 : overview_count_;
    }

    uint8_t ZoomCycle::rank_offset() const
    {
        return zoomed_rank();
    }

} // namespace gui
