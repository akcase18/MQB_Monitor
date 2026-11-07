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

        if (count == 5 && orientation == LayoutOrientation::Vertical)
        {
            /**
             * 3 rows:
             * - Row 1: 2 small panels
             * - Row 2: 2 small panels
             * - Row 3: 1 larger panel
             */
            out[0] = { 0, 0, 1, 1, false };
            out[1] = { 1, 0, 1, 1, false };
            out[2] = { 0, 1, 1, 1, false };
            out[3] = { 1, 1, 1, 1, false };
            out[4] = { 0, 2, 2, 1, true };
            return count;
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
            /**
             * 2 columns:
             * - Column 1: Narrow, 2 stacked small panels
             * - Column 2: Wider, 1 large, full-height panel
             */
            out[0] = { 0, 0, STACK_COLSPAN, 1, false };
            out[1] = { 0, 1, STACK_COLSPAN, 1, false };
            out[2] = { STACK_COLSPAN, 0, HERO_COLSPAN, 2, true };
        }
        else
        {
            uint8_t small_count = static_cast<uint8_t>((count + 1) / 2);
            uint8_t large_count = static_cast<uint8_t>(count - small_count);

            fill_band(out, 0, small_count, 0, false);
            fill_band(out, small_count, large_count, 1, true);

            /**
             * 4 and 6 are even splits where every cell is the same size.
             */
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

    bool is_hero_slot(const PanelSlot &slot)
    {
        /**
         * transpose() swaps colspan/rowspan wholesale for Vertical orientation,
         * so a check pinned to one field (e.g. "rowspan == 2") only recognizes
         * hero slots in Horizontal. The unordered pair {colspan, rowspan} is
         * transpose-invariant, so compare by min/max instead of by axis.
         */
        uint8_t span_max
            = slot.colspan > slot.rowspan ? slot.colspan : slot.rowspan;
        uint8_t span_min
            = slot.colspan > slot.rowspan ? slot.rowspan : slot.colspan;
        return span_min == 2 && span_max >= 4;
    }

    void compute_prominence_ranks(const PanelSlot slots[MAX_PANELS],
                                  uint8_t         count,
                                  uint8_t         rank_of_slot[MAX_PANELS])
    {
        uint8_t rank = 0;

        /**
         * Pass 0: hero. Pass 1: large-but-not-hero. Pass 2: everything
         * else (small). Within a pass, slots are visited in their
         * existing index order, which compute_layout() already produces
         * left-to-right/top-to-bottom.
         */
        for (uint8_t pass = 0; pass < 3; pass++)
        {
            for (uint8_t i = 0; i < count; i++)
            {
                bool    hero = is_hero_slot(slots[i]);
                uint8_t tier = hero ? 0 : (slots[i].large ? 1 : 2);
                if (tier == pass)
                {
                    rank_of_slot[i] = rank++;
                }
            }
        }
    }

} // namespace gui
