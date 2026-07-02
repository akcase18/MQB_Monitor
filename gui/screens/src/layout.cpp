#include "layout.hpp"

namespace gui
{

    namespace
    {
        constexpr uint8_t BAND_UNITS = 6;

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

        if (count <= 3)
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
        else
        {
            uint8_t small_count
                = static_cast<uint8_t>((count + 1) / 2); /* ceil */
            uint8_t large_count = static_cast<uint8_t>(count - small_count);

            fill_band(out, 0, small_count, 0, false);
            fill_band(out, small_count, large_count, 1, true);
        }

        if (orientation == LayoutOrientation::Vertical)
        {
            transpose(out, count);
        }

        return count;
    }

} // namespace gui
