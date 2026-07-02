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
     * given orientation.
     *
     * count <= 3: a single full-span band (`large` = true), split evenly.
     * count >= 4: ceil(count/2) panels in the small band, the rest in the
     * large band — e.g. count=5 gives 3 small + 2 large, matching the old
     * fixed 5-panel layout exactly.
     *
     * @returns The number of slots written to `out` (the clamped count).
     */
    uint8_t compute_layout(uint8_t           count,
                           LayoutOrientation orientation,
                           PanelSlot         out[MAX_PANELS]);

} // namespace gui
