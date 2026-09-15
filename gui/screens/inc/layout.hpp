/**
 * @file layout.hpp
 * @brief Pure grid-slot placement for 1-6 panels — no LVGL, no core::, so
 * it's cheap to unit test and easy to swap for a different algorithm
 * later without touching InfoScreen.
 */
#pragma once

#include <stdint.h>

namespace gui
{

    constexpr uint8_t MAX_PANELS = 6;
    constexpr uint8_t BAND_UNITS = 6;

    /**
     * count == 3's hero/stack width split, in units of BAND_UNITS — a
     * starting point pending an on-hardware check of what actually reads
     * well at arm's length. Adjust both together; they must sum to
     * BAND_UNITS.
     */
    constexpr uint8_t HERO_COLSPAN  = 4;
    constexpr uint8_t STACK_COLSPAN = BAND_UNITS - HERO_COLSPAN;

    enum class LayoutOrientation
    {
        Horizontal,
        Vertical
    };

    /**
     * How the panel count is turned into a shape. A style axis rather than
     * a replacement, because hero-plus-stack is a deliberate design on a
     * 480x320 panel and equal thirds is a deliberate design on a 320x172
     * strip -- the two products want different answers from the same
     * function.
     */
    enum class LayoutStyle
    {
        /** Bands, a hero at count 3, the 45/55 split. What every existing
         * caller gets, byte-for-byte. */
        Grid,

        /**
         * N equal bands, every panel using the full extent of the cross
         * axis. For a short, wide panel where **no panel can afford to
         * give up vertical space to a neighbour**: at 172 px tall,
         * hero-plus-stack leaves the stacked cells roughly 77 and 95 px,
         * and once panel_border_width and panel_gap come out there is not
         * enough left for a label and a value.
         *
         * Well-defined only for counts that divide BAND_UNITS -- 1, 2, 3
         * and 6. Any other count falls through to Grid, which is a real
         * shape rather than a broken one. The gauge product clamps to 3,
         * so the limit never bites there.
         *
         * Note this leaves **no slot a hero** at count 3, since every slot
         * is 2x2. That is correct: there is no hero here, and
         * compute_prominence_ranks() consequently orders slots plain
         * left-to-right, so priority rank 0 lands leftmost.
         */
        Strip
    };

    /**
     * A slot's position within a two-band grid: a "small" band and a "large"
     * band, laid out side by side along the orientation's main axis. For
     * Horizontal, `col`/`colspan` divide a 6-unit-wide grid and `row` selects
     * which of the 2 bands (0 = small/top, 1 = large/bottom); for Vertical the
     * axes are transposed (row/rowspan divide a 6-unit-tall grid, col selects
     * small/left vs large/right).
     */
    struct PanelSlot
    {
            uint8_t col;
            uint8_t row;
            uint8_t colspan;
            uint8_t rowspan;
            bool    large;
    };

    /**
     * Computes slot positions for `count` panels (clamped to 1-6) in the
     * given orientation. Each count gets its own shape:
     *
     * 1: one full-screen panel.
     * 2: two equal full-height columns.
     * 3: a wide hero (large) plus two stacked minors (small) — not three
     *    narrow columns.
     * 4: an even 2x2, all panels the same size (large).
     * 5: Horizontal — 3 small panels over 2 large ones, matching the old
     *    fixed layout. Vertical — its own 2x2 of small panels over one
     *    wider, higher-priority panel (not a transpose of Horizontal's).
     * 6: an even 2x3, all panels the same size (small).
     *
     * Those shapes are LayoutStyle::Grid. LayoutStyle::Strip instead
     * gives `count` equal full-height bands -- see LayoutStyle.
     *
     * @returns The number of slots written to `out` (the clamped count).
     */
    uint8_t compute_layout(uint8_t           count,
                           LayoutOrientation orientation,
                           LayoutStyle       style,
                           PanelSlot         out[MAX_PANELS]);

    /**
     * True for the truly roomy full-height slots — count 1's panel and
     * count 3's hero — not count 2's narrower full-height columns.
     */
    bool is_hero_slot(const PanelSlot &slot);

    /**
     * For each of the `count` slots compute_layout() just produced, which
     * priority rank (0 = most important) should be displayed there.
     * Prominence order is hero > large > small; within a tier, existing
     * slot index order (already left-to-right/top-to-bottom by
     * construction, see fill_band() in layout.cpp) — three passes over
     * [0,count), no sort needed.
     */
    void compute_prominence_ranks(const PanelSlot slots[MAX_PANELS],
                                  uint8_t         count,
                                  uint8_t         rank_of_slot[MAX_PANELS]);

} // namespace gui
