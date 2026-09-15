/**
 * @file zoom_cycle.hpp
 * @brief Which of the overview's panels, if any, is currently filling the
 * screen.
 *
 * Equal thirds (see LayoutStyle::Strip) means no panel is a hero, so the
 * MQB product's "promote this one to the big slot" gesture has nothing to
 * promote to. The replacement on a short, wide strip is a cycle:
 *
 *     3-up  ->  rank 0 full-screen  ->  rank 1  ->  rank 2  ->  3-up
 *
 * which needs no new layout work at all -- full-screen is
 * `compute_layout(1, Horizontal, Strip)`, one panel at the full panel
 * size, and it is where the largest data face in the ladder earns its
 * place.
 *
 * Pure state, no LVGL and no core::, so the cycle's edges are testable
 * without a screen. The composition root asks it three questions after
 * every advance -- how many panels, which rank leads, are we zoomed --
 * and hands the answers to InfoScreen.
 */
#pragma once

#include <stdint.h>

#include "layout.hpp"

namespace gui
{

    class ZoomCycle
    {
        public:
            /**
             * How many panels the overview shows. Clamped to 1..MAX_PANELS.
             *
             * Returning to the overview whenever this changes is
             * deliberate: the panel count changing means the set being
             * cycled through has changed, and holding a zoom on what used
             * to be rank 2 is not a state worth preserving across it.
             */
            void set_overview_count(uint8_t count);

            /**
             * One step around the cycle: overview -> rank 0 -> ... ->
             * rank n-1 -> overview.
             *
             * **A no-op when the overview is a single panel.** Zooming
             * into rank 0 from a one-panel overview is the same view
             * twice, so the cycle would have a step where the gesture
             * appears not to work. One panel means one position.
             */
            void advance();

            /** Straight back to the overview, wherever the cycle is. */
            void reset();

            bool zoomed() const;

            /** Which priority rank is filling the screen. Zero, and
             * meaningless, unless zoomed(). */
            uint8_t zoomed_rank() const;

            /** What to pass InfoScreen::rebuild() -- 1 while zoomed, the
             * overview count otherwise. */
            uint8_t panel_count() const;

            /** What to pass InfoScreen::set_rank_offset() -- the zoomed
             * rank while zoomed, 0 otherwise. */
            uint8_t rank_offset() const;

        private:
            uint8_t overview_count_ = 1;

            /** 0 is the overview; 1..overview_count_ is zoomed on rank
             * position_ - 1. Stored with the offset rather than as a
             * signed "-1 means overview" so there is no sentinel to get
             * wrong. */
            uint8_t position_ = 0;
    };

} // namespace gui
