#include "layout.hpp"

namespace gui
{

    namespace
    {
        void fill_band(PanelSlot *out,
                       uint8_t    start_index,
                       uint8_t    band_count,
                       uint8_t    band,
                       bool       large)
        {
            uint8_t colspan = static_cast<uint8_t>(BAND_UNITS / band_count);
            for (uint8_t i = 0; i < band_count; i++)
            {
                PanelSlot &slot = out[start_index + i];
                slot.col        = static_cast<uint8_t>(i * colspan);
                slot.row        = band;
                slot.colspan    = colspan;
                slot.rowspan    = 1;
                slot.large      = large;
            }
        }

        void transpose(PanelSlot out[MAX_PANELS], uint8_t count)
        {
            for (uint8_t i = 0; i < count; i++)
            {
                PanelSlot &slot    = out[i];
                uint8_t    col     = slot.col;
                uint8_t    colspan = slot.colspan;
                slot.col           = slot.row;
                slot.colspan       = slot.rowspan;
                slot.row           = col;
                slot.rowspan       = colspan;
            }
        }
    } // namespace

    uint8_t compute_layout(uint8_t           count,
                           LayoutOrientation orientation,
                           PanelSlot         out[MAX_PANELS])
    {
        if (count < 1)
        {
            count = 1;
        }
        else if (count > MAX_PANELS)
        {
            count = MAX_PANELS;
        }

        if (count <= 2)
        {
            uint8_t colspan = static_cast<uint8_t>(BAND_UNITS / count);
            for (uint8_t i = 0; i < count; i++)
            {
                out[i].col     = static_cast<uint8_t>(i * colspan);
                out[i].row     = 0;
                out[i].colspan = colspan;
                out[i].rowspan = 2;
                out[i].large   = true;
            }
        }
        else if (count == 3)
        {
            /* Two stacked minors on the left, one wide hero on the right —
             * lower indices are the smaller panels, matching the small-
             * band-first convention used by the count > 3 case below. */
            out[0] = { 0, 0, STACK_COLSPAN, 1, false };
            out[1] = { 0, 1, STACK_COLSPAN, 1, false };
            out[2] = { STACK_COLSPAN, 0, HERO_COLSPAN, 2, true };
        }
        else
        {
            uint8_t small_count
                = static_cast<uint8_t>((count + 1) / 2); /* ceil */
            uint8_t large_count = static_cast<uint8_t>(count - small_count);

            fill_band(out, 0, small_count, 0, false);
            fill_band(out, small_count, large_count, 1, true);

            /* 4 and 6 are even splits where every cell is the same size —
             * unlike 5 (today's fixed layout, kept as-is), there's no
             * small/large distinction to preserve here. */
            if (count == 4)
            {
                for (uint8_t i = 0; i < count; i++)
                {
                    out[i].large = true;
                }
            }
            else if (count == 6)
            {
                for (uint8_t i = 0; i < count; i++)
                {
                    out[i].large = false;
                }
            }
        }

        if (orientation == LayoutOrientation::Vertical)
        {
            transpose(out, count);
        }

        return count;
    }

} // namespace gui
