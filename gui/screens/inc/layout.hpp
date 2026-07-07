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
     * 5: 3 small panels over 2 large ones, matching the old fixed layout.
     * 6: an even 2x3, all panels the same size (small).
     *
     * @returns The number of slots written to `out` (the clamped count).
     */
    uint8_t compute_layout(uint8_t           count,
                           LayoutOrientation orientation,
                           PanelSlot         out[MAX_PANELS]);

} // namespace gui
