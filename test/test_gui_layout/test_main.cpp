/**
 * @file test_main.cpp
 * @brief Host-buildable coverage for gui/screens::compute_layout(): full
 * coverage/no-overlap of the band grid for every panel count (1-6) in
 * both orientations, the exact old fixed-5-panel split (Horizontal), and
 * Vertical's own bespoke 5-panel shape (not a transpose of Horizontal's —
 * see test_five_panels_vertical_is_2x2_plus_wide_row()). Run with `pio
 * test -e simulator`.
 */
#include <unity.h>

#include "layout.hpp"
#include "zoom_cycle.hpp"

void setUp()
{
}
void tearDown()
{
}

namespace
{
    /**
     * Sums each slot's column footprint per row and confirms every row used
     * is fully covered (colspans sum to the band width) with no overlaps
     * (each row is only ever covered by slots that don't share columns —
     * verified here by the sum check, since overlapping slots would double
     * count and a gap would undercount). */
    void assert_full_row_coverage(const gui::PanelSlot *slots,
                                  uint8_t               count,
                                  uint8_t               band_width)
    {
        for (uint8_t row = 0; row < 2; row++)
        {
            uint16_t covered = 0;
            for (uint8_t i = 0; i < count; i++)
            {
                if (slots[i].row == row
                    || (slots[i].row == 0 && slots[i].rowspan == 2 && row == 1))
                {
                    covered += slots[i].colspan;
                }
            }
            TEST_ASSERT_TRUE(covered == 0 || covered == band_width);
        }
    }
} // namespace

void test_single_panel_spans_full_screen()
{
    gui::PanelSlot slots[gui::MAX_PANELS];
    uint8_t        n
        = gui::compute_layout(1, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);

    TEST_ASSERT_EQUAL_UINT8(1, n);
    TEST_ASSERT_EQUAL_UINT8(0, slots[0].col);
    TEST_ASSERT_EQUAL_UINT8(0, slots[0].row);
    TEST_ASSERT_EQUAL_UINT8(6, slots[0].colspan);
    TEST_ASSERT_EQUAL_UINT8(2, slots[0].rowspan);
    TEST_ASSERT_TRUE(slots[0].large);
}

void test_five_panels_matches_old_fixed_layout()
{
    gui::PanelSlot slots[gui::MAX_PANELS];
    uint8_t        n
        = gui::compute_layout(5, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);

    TEST_ASSERT_EQUAL_UINT8(5, n);

    /* 3 small panels on top, each 1/3 width (colspan 2 of 6). */
    for (uint8_t i = 0; i < 3; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, slots[i].row);
        TEST_ASSERT_EQUAL_UINT8(2, slots[i].colspan);
        TEST_ASSERT_EQUAL_UINT8(1, slots[i].rowspan);
        TEST_ASSERT_FALSE(slots[i].large);
    }

    /* 2 large panels on bottom, each 1/2 width (colspan 3 of 6). */
    for (uint8_t i = 3; i < 5; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(1, slots[i].row);
        TEST_ASSERT_EQUAL_UINT8(3, slots[i].colspan);
        TEST_ASSERT_EQUAL_UINT8(1, slots[i].rowspan);
        TEST_ASSERT_TRUE(slots[i].large);
    }
}

void test_two_panels_are_equal_full_height_columns()
{
    gui::PanelSlot slots[gui::MAX_PANELS];
    uint8_t        n
        = gui::compute_layout(2, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);

    TEST_ASSERT_EQUAL_UINT8(2, n);
    for (uint8_t i = 0; i < 2; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, slots[i].row);
        TEST_ASSERT_EQUAL_UINT8(3, slots[i].colspan);
        TEST_ASSERT_EQUAL_UINT8(2, slots[i].rowspan);
        TEST_ASSERT_TRUE(slots[i].large);
    }
    TEST_ASSERT_EQUAL_UINT8(0, slots[0].col);
    TEST_ASSERT_EQUAL_UINT8(3, slots[1].col);
}

void test_three_panels_are_hero_plus_stack()
{
    gui::PanelSlot slots[gui::MAX_PANELS];
    uint8_t        n
        = gui::compute_layout(3, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);

    TEST_ASSERT_EQUAL_UINT8(3, n);

    /**
     * Two stacked minors on the left, lower indices per the small-first
     * convention. */
    TEST_ASSERT_EQUAL_UINT8(0, slots[0].col);
    TEST_ASSERT_EQUAL_UINT8(0, slots[0].row);
    TEST_ASSERT_EQUAL_UINT8(gui::STACK_COLSPAN, slots[0].colspan);
    TEST_ASSERT_EQUAL_UINT8(1, slots[0].rowspan);
    TEST_ASSERT_FALSE(slots[0].large);

    TEST_ASSERT_EQUAL_UINT8(0, slots[1].col);
    TEST_ASSERT_EQUAL_UINT8(1, slots[1].row);
    TEST_ASSERT_EQUAL_UINT8(gui::STACK_COLSPAN, slots[1].colspan);
    TEST_ASSERT_EQUAL_UINT8(1, slots[1].rowspan);
    TEST_ASSERT_FALSE(slots[1].large);

    /* Wide hero on the right, spanning both rows. */
    TEST_ASSERT_EQUAL_UINT8(gui::STACK_COLSPAN, slots[2].col);
    TEST_ASSERT_EQUAL_UINT8(0, slots[2].row);
    TEST_ASSERT_EQUAL_UINT8(gui::HERO_COLSPAN, slots[2].colspan);
    TEST_ASSERT_EQUAL_UINT8(2, slots[2].rowspan);
    TEST_ASSERT_TRUE(slots[2].large);
}

void test_four_panels_are_equal_2x2()
{
    gui::PanelSlot slots[gui::MAX_PANELS];
    uint8_t        n
        = gui::compute_layout(4, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);

    TEST_ASSERT_EQUAL_UINT8(4, n);
    for (uint8_t i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(3, slots[i].colspan);
        TEST_ASSERT_EQUAL_UINT8(1, slots[i].rowspan);
        TEST_ASSERT_TRUE(slots[i].large);
    }
}

void test_six_panels_are_equal_2x3()
{
    gui::PanelSlot slots[gui::MAX_PANELS];
    uint8_t        n
        = gui::compute_layout(6, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);

    TEST_ASSERT_EQUAL_UINT8(6, n);
    for (uint8_t i = 0; i < 6; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(2, slots[i].colspan);
        TEST_ASSERT_EQUAL_UINT8(1, slots[i].rowspan);
        TEST_ASSERT_FALSE(slots[i].large);
    }
}

void test_horizontal_full_coverage_for_every_count()
{
    for (uint8_t count = 1; count <= gui::MAX_PANELS; count++)
    {
        gui::PanelSlot slots[gui::MAX_PANELS];
        uint8_t        n = gui::compute_layout(
            count, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);
        TEST_ASSERT_EQUAL_UINT8(count, n);
        assert_full_row_coverage(slots, n, 6);
    }
}

void test_vertical_is_a_transpose_of_horizontal()
{
    for (uint8_t count = 1; count <= gui::MAX_PANELS; count++)
    {
        /**
         * 5 is the one exception — see
         * test_five_panels_vertical_is_2x2_plus_wide_row() below for why a
         * straight transpose doesn't apply to it. */
        if (count == 5)
        {
            continue;
        }

        gui::PanelSlot h_slots[gui::MAX_PANELS];
        gui::PanelSlot v_slots[gui::MAX_PANELS];
        gui::compute_layout(count, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, h_slots);
        gui::compute_layout(count, gui::LayoutOrientation::Vertical, gui::LayoutStyle::Grid, v_slots);

        for (uint8_t i = 0; i < count; i++)
        {
            TEST_ASSERT_EQUAL_UINT8(h_slots[i].row, v_slots[i].col);
            TEST_ASSERT_EQUAL_UINT8(h_slots[i].col, v_slots[i].row);
            TEST_ASSERT_EQUAL_UINT8(h_slots[i].rowspan, v_slots[i].colspan);
            TEST_ASSERT_EQUAL_UINT8(h_slots[i].colspan, v_slots[i].rowspan);
            TEST_ASSERT_EQUAL(h_slots[i].large, v_slots[i].large);
        }
    }
}

void test_five_panels_vertical_is_2x2_plus_wide_row()
{
    gui::PanelSlot slots[gui::MAX_PANELS];
    uint8_t        n
        = gui::compute_layout(5, gui::LayoutOrientation::Vertical, gui::LayoutStyle::Grid, slots);

    TEST_ASSERT_EQUAL_UINT8(5, n);

    /**
     * A straight transpose of Horizontal's 5 (3 small over 2 large) would
     * stack 3 skinny panels in one column and 2 in the other — instead,
     * Vertical's 5 gets its own shape: a 2x2 of small panels over one
     * wider, higher-priority panel (see layout.cpp's compute_layout()). */
    TEST_ASSERT_EQUAL_UINT8(0, slots[0].col);
    TEST_ASSERT_EQUAL_UINT8(0, slots[0].row);
    TEST_ASSERT_EQUAL_UINT8(1, slots[0].colspan);
    TEST_ASSERT_EQUAL_UINT8(1, slots[0].rowspan);
    TEST_ASSERT_FALSE(slots[0].large);

    TEST_ASSERT_EQUAL_UINT8(1, slots[1].col);
    TEST_ASSERT_EQUAL_UINT8(0, slots[1].row);
    TEST_ASSERT_EQUAL_UINT8(1, slots[1].colspan);
    TEST_ASSERT_EQUAL_UINT8(1, slots[1].rowspan);
    TEST_ASSERT_FALSE(slots[1].large);

    TEST_ASSERT_EQUAL_UINT8(0, slots[2].col);
    TEST_ASSERT_EQUAL_UINT8(1, slots[2].row);
    TEST_ASSERT_EQUAL_UINT8(1, slots[2].colspan);
    TEST_ASSERT_EQUAL_UINT8(1, slots[2].rowspan);
    TEST_ASSERT_FALSE(slots[2].large);

    TEST_ASSERT_EQUAL_UINT8(1, slots[3].col);
    TEST_ASSERT_EQUAL_UINT8(1, slots[3].row);
    TEST_ASSERT_EQUAL_UINT8(1, slots[3].colspan);
    TEST_ASSERT_EQUAL_UINT8(1, slots[3].rowspan);
    TEST_ASSERT_FALSE(slots[3].large);

    /**
     * The wide, higher-priority bottom panel — full width (colspan 2),
     * one row tall. */
    TEST_ASSERT_EQUAL_UINT8(0, slots[4].col);
    TEST_ASSERT_EQUAL_UINT8(2, slots[4].row);
    TEST_ASSERT_EQUAL_UINT8(2, slots[4].colspan);
    TEST_ASSERT_EQUAL_UINT8(1, slots[4].rowspan);
    TEST_ASSERT_TRUE(slots[4].large);
}

void test_hero_slot_extraction_matches_table()
{
    gui::PanelSlot slots[gui::MAX_PANELS];

    /* Count 1: the sole full-screen panel is a hero. */
    gui::compute_layout(1, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);
    TEST_ASSERT_TRUE(gui::is_hero_slot(slots[0]));

    /* Count 2: full-height but only 1/2 width — large, not hero. */
    gui::compute_layout(2, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);
    TEST_ASSERT_FALSE(gui::is_hero_slot(slots[0]));
    TEST_ASSERT_FALSE(gui::is_hero_slot(slots[1]));

    /**
     * Count 3: slot 2 (the wide right panel) is the hero; the two
     * stacked minors are not. */
    gui::compute_layout(3, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);
    TEST_ASSERT_FALSE(gui::is_hero_slot(slots[0]));
    TEST_ASSERT_FALSE(gui::is_hero_slot(slots[1]));
    TEST_ASSERT_TRUE(gui::is_hero_slot(slots[2]));

    /* Counts 4/5/6 have no rowspan-2 slots at all — no hero anywhere. */
    for (uint8_t count = 4; count <= 6; count++)
    {
        gui::compute_layout(count, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);
        for (uint8_t i = 0; i < count; i++)
        {
            TEST_ASSERT_FALSE(gui::is_hero_slot(slots[i]));
        }
    }
}

void test_prominence_rank_count1()
{
    gui::PanelSlot slots[gui::MAX_PANELS];
    uint8_t        rank_of_slot[gui::MAX_PANELS];
    gui::compute_layout(1, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);
    gui::compute_prominence_ranks(slots, 1, rank_of_slot);

    TEST_ASSERT_EQUAL_UINT8(0, rank_of_slot[0]);
}

void test_prominence_rank_count2()
{
    gui::PanelSlot slots[gui::MAX_PANELS];
    uint8_t        rank_of_slot[gui::MAX_PANELS];
    gui::compute_layout(2, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);
    gui::compute_prominence_ranks(slots, 2, rank_of_slot);

    /* Both slots are tied (large, not hero) — tie-break is index order. */
    TEST_ASSERT_EQUAL_UINT8(0, rank_of_slot[0]);
    TEST_ASSERT_EQUAL_UINT8(1, rank_of_slot[1]);
}

void test_prominence_rank_count3()
{
    gui::PanelSlot slots[gui::MAX_PANELS];
    uint8_t        rank_of_slot[gui::MAX_PANELS];
    gui::compute_layout(3, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);
    gui::compute_prominence_ranks(slots, 3, rank_of_slot);

    /**
     * Slot 2 (hero) is rank 0 despite being the last index — this is the
     * exact case motivating compute_prominence_ranks() to exist. */
    TEST_ASSERT_EQUAL_UINT8(1, rank_of_slot[0]);
    TEST_ASSERT_EQUAL_UINT8(2, rank_of_slot[1]);
    TEST_ASSERT_EQUAL_UINT8(0, rank_of_slot[2]);
}

void test_prominence_rank_count4()
{
    gui::PanelSlot slots[gui::MAX_PANELS];
    uint8_t        rank_of_slot[gui::MAX_PANELS];
    gui::compute_layout(4, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);
    gui::compute_prominence_ranks(slots, 4, rank_of_slot);

    /* All 4 tied (large, not hero) — index order throughout. */
    for (uint8_t i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(i, rank_of_slot[i]);
    }
}

void test_prominence_rank_count5()
{
    gui::PanelSlot slots[gui::MAX_PANELS];
    uint8_t        rank_of_slot[gui::MAX_PANELS];
    gui::compute_layout(5, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);
    gui::compute_prominence_ranks(slots, 5, rank_of_slot);

    /**
     * Slots 3,4 (the large bottom band) outrank slots 0,1,2 (the small
     * top band) despite having later indices — the other case motivating
     * compute_prominence_ranks() to exist. */
    TEST_ASSERT_EQUAL_UINT8(2, rank_of_slot[0]);
    TEST_ASSERT_EQUAL_UINT8(3, rank_of_slot[1]);
    TEST_ASSERT_EQUAL_UINT8(4, rank_of_slot[2]);
    TEST_ASSERT_EQUAL_UINT8(0, rank_of_slot[3]);
    TEST_ASSERT_EQUAL_UINT8(1, rank_of_slot[4]);
}

void test_prominence_rank_count6()
{
    gui::PanelSlot slots[gui::MAX_PANELS];
    uint8_t        rank_of_slot[gui::MAX_PANELS];
    gui::compute_layout(6, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots);
    gui::compute_prominence_ranks(slots, 6, rank_of_slot);

    /* All 6 tied (small, not hero/large) — index order throughout. */
    for (uint8_t i = 0; i < 6; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(i, rank_of_slot[i]);
    }
}

void test_prominence_rank_matches_horizontal_under_vertical_transpose()
{
    for (uint8_t count = 1; count <= gui::MAX_PANELS; count++)
    {
        /**
         * 5's Vertical shape isn't a transpose of Horizontal's (see
         * test_five_panels_vertical_is_2x2_plus_wide_row()), so there's no
         * reason its prominence ranks would match either — covered
         * separately by test_prominence_rank_count5_vertical(). */
        if (count == 5)
        {
            continue;
        }

        gui::PanelSlot h_slots[gui::MAX_PANELS];
        gui::PanelSlot v_slots[gui::MAX_PANELS];
        gui::compute_layout(count, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, h_slots);
        gui::compute_layout(count, gui::LayoutOrientation::Vertical, gui::LayoutStyle::Grid, v_slots);

        uint8_t h_rank[gui::MAX_PANELS];
        uint8_t v_rank[gui::MAX_PANELS];
        gui::compute_prominence_ranks(h_slots, count, h_rank);
        gui::compute_prominence_ranks(v_slots, count, v_rank);

        for (uint8_t i = 0; i < count; i++)
        {
            TEST_ASSERT_EQUAL_UINT8(h_rank[i], v_rank[i]);
        }
    }
}

void test_prominence_rank_count5_vertical()
{
    gui::PanelSlot slots[gui::MAX_PANELS];
    uint8_t        rank_of_slot[gui::MAX_PANELS];
    gui::compute_layout(5, gui::LayoutOrientation::Vertical, gui::LayoutStyle::Grid, slots);
    gui::compute_prominence_ranks(slots, 5, rank_of_slot);

    /**
     * Slot 4 (the wide bottom panel, large) outranks slots 0-3 (the 2x2 of
     * small panels) despite being the last index. */
    TEST_ASSERT_EQUAL_UINT8(1, rank_of_slot[0]);
    TEST_ASSERT_EQUAL_UINT8(2, rank_of_slot[1]);
    TEST_ASSERT_EQUAL_UINT8(3, rank_of_slot[2]);
    TEST_ASSERT_EQUAL_UINT8(4, rank_of_slot[3]);
    TEST_ASSERT_EQUAL_UINT8(0, rank_of_slot[4]);
}

void test_count_is_clamped_to_valid_range()
{
    gui::PanelSlot slots[gui::MAX_PANELS];

    TEST_ASSERT_EQUAL_UINT8(
        1, gui::compute_layout(0, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots));
    TEST_ASSERT_EQUAL_UINT8(
        gui::MAX_PANELS,
        gui::compute_layout(255, gui::LayoutOrientation::Horizontal, gui::LayoutStyle::Grid, slots));
}


/**
 * ==============================================================================
 * LayoutStyle
 =============================================================================*/

/**
 * Every Grid slot for every count and orientation, captured from the
 * implementation as it stood *before* LayoutStyle existed.
 *
 * This is the regression net that protects the MQB product from the new
 * style axis. The other tests in this file assert properties -- tiling,
 * transposition, clamping -- which a subtly different but still
 * self-consistent shape could satisfy. This one asserts the actual
 * numbers, so any change to Grid at all has to be deliberate enough to
 * come here and edit a table.
 */
namespace
{
    struct GoldenCase
    {
            int            orientation; /* 0 = Horizontal, 1 = Vertical */
            int            count;
            gui::PanelSlot slots[gui::MAX_PANELS];
    };

    const GoldenCase GRID_GOLDEN[] = {
        /* H count 1 */
        { 0, 1, { { 0, 0, 6, 2, true }, } },
        /* H count 2 */
        { 0, 2, { { 0, 0, 3, 2, true }, { 3, 0, 3, 2, true }, } },
        /* H count 3 */
        { 0, 3, { { 0, 0, 2, 1, false }, { 0, 1, 2, 1, false }, { 2, 0, 4, 2, true }, } },
        /* H count 4 */
        { 0, 4, { { 0, 0, 3, 1, true }, { 3, 0, 3, 1, true }, { 0, 1, 3, 1, true }, { 3, 1, 3, 1, true }, } },
        /* H count 5 */
        { 0, 5, { { 0, 0, 2, 1, false }, { 2, 0, 2, 1, false }, { 4, 0, 2, 1, false }, { 0, 1, 3, 1, true }, { 3, 1, 3, 1, true }, } },
        /* H count 6 */
        { 0, 6, { { 0, 0, 2, 1, false }, { 2, 0, 2, 1, false }, { 4, 0, 2, 1, false }, { 0, 1, 2, 1, false }, { 2, 1, 2, 1, false }, { 4, 1, 2, 1, false }, } },
        /* V count 1 */
        { 1, 1, { { 0, 0, 2, 6, true }, } },
        /* V count 2 */
        { 1, 2, { { 0, 0, 2, 3, true }, { 0, 3, 2, 3, true }, } },
        /* V count 3 */
        { 1, 3, { { 0, 0, 1, 2, false }, { 1, 0, 1, 2, false }, { 0, 2, 2, 4, true }, } },
        /* V count 4 */
        { 1, 4, { { 0, 0, 1, 3, true }, { 0, 3, 1, 3, true }, { 1, 0, 1, 3, true }, { 1, 3, 1, 3, true }, } },
        /* V count 5 */
        { 1, 5, { { 0, 0, 1, 1, false }, { 1, 0, 1, 1, false }, { 0, 1, 1, 1, false }, { 1, 1, 1, 1, false }, { 0, 2, 2, 1, true }, } },
        /* V count 6 */
        { 1, 6, { { 0, 0, 1, 2, false }, { 0, 2, 1, 2, false }, { 0, 4, 1, 2, false }, { 1, 0, 1, 2, false }, { 1, 2, 1, 2, false }, { 1, 4, 1, 2, false }, } },
    };
} // namespace

void test_grid_output_is_byte_identical_to_before_the_style_axis(void)
{
    for (const GoldenCase &c : GRID_GOLDEN)
    {
        gui::PanelSlot got[gui::MAX_PANELS];
        uint8_t        n = gui::compute_layout(
            static_cast<uint8_t>(c.count),
            c.orientation == 0 ? gui::LayoutOrientation::Horizontal
                                      : gui::LayoutOrientation::Vertical,
            gui::LayoutStyle::Grid,
            got);

        TEST_ASSERT_EQUAL_UINT8(c.count, n);
        for (uint8_t i = 0; i < n; i++)
        {
            TEST_ASSERT_EQUAL_UINT8(c.slots[i].col, got[i].col);
            TEST_ASSERT_EQUAL_UINT8(c.slots[i].row, got[i].row);
            TEST_ASSERT_EQUAL_UINT8(c.slots[i].colspan, got[i].colspan);
            TEST_ASSERT_EQUAL_UINT8(c.slots[i].rowspan, got[i].rowspan);
            TEST_ASSERT_EQUAL(c.slots[i].large, got[i].large);
        }
    }
}

void test_strip_gives_equal_full_height_bands(void)
{
    /**
     * The gauge product's shape: three 2x2 cells across a 6-unit axis,
     * every one using the full height. Hero-plus-stack would leave the
     * stacked cells ~77 and ~95 px of a 172 px panel, which is not enough
     * for a label and a value once the border and gap come out. */
    gui::PanelSlot slots[gui::MAX_PANELS];
    uint8_t        n = gui::compute_layout(3,
                                    gui::LayoutOrientation::Horizontal,
                                    gui::LayoutStyle::Strip,
                                    slots);

    TEST_ASSERT_EQUAL_UINT8(3, n);
    for (uint8_t i = 0; i < 3; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(i * 2, slots[i].col);
        TEST_ASSERT_EQUAL_UINT8(0, slots[i].row);
        TEST_ASSERT_EQUAL_UINT8(2, slots[i].colspan);
        TEST_ASSERT_EQUAL_UINT8(2, slots[i].rowspan);
        TEST_ASSERT_TRUE(slots[i].large);
    }
}

void test_strip_tiles_completely_for_every_divisor_of_the_band(void)
{
    const uint8_t divisors[] = { 1, 2, 3, 6 };
    for (uint8_t count : divisors)
    {
        gui::PanelSlot slots[gui::MAX_PANELS];
        gui::compute_layout(count,
                            gui::LayoutOrientation::Horizontal,
                            gui::LayoutStyle::Strip,
                            slots);

        /* Every one of the 6 x 2 cells covered exactly once. */
        uint8_t cover[gui::BAND_UNITS][2] = { { 0 } };
        for (uint8_t i = 0; i < count; i++)
        {
            for (uint8_t c = 0; c < slots[i].colspan; c++)
            {
                for (uint8_t r = 0; r < slots[i].rowspan; r++)
                {
                    cover[slots[i].col + c][slots[i].row + r]++;
                }
            }
        }
        for (uint8_t c = 0; c < gui::BAND_UNITS; c++)
        {
            TEST_ASSERT_EQUAL_UINT8(1, cover[c][0]);
            TEST_ASSERT_EQUAL_UINT8(1, cover[c][1]);
        }
    }
}

void test_strip_falls_through_to_grid_for_an_indivisible_count(void)
{
    /**
     * 4 and 5 do not divide a 6-unit band. Falling through gives a real
     * shape rather than a grid with a hole in it. This product clamps to
     * 3 so the limit never bites, but the function must still be total. */
    for (uint8_t count = 4; count <= 5; count++)
    {
        gui::PanelSlot strip[gui::MAX_PANELS];
        gui::PanelSlot grid[gui::MAX_PANELS];
        gui::compute_layout(count,
                            gui::LayoutOrientation::Horizontal,
                            gui::LayoutStyle::Strip,
                            strip);
        gui::compute_layout(count,
                            gui::LayoutOrientation::Horizontal,
                            gui::LayoutStyle::Grid,
                            grid);

        for (uint8_t i = 0; i < count; i++)
        {
            TEST_ASSERT_EQUAL_UINT8(grid[i].col, strip[i].col);
            TEST_ASSERT_EQUAL_UINT8(grid[i].row, strip[i].row);
            TEST_ASSERT_EQUAL_UINT8(grid[i].colspan, strip[i].colspan);
            TEST_ASSERT_EQUAL_UINT8(grid[i].rowspan, strip[i].rowspan);
        }
    }
}

void test_strip_has_no_hero_so_ranks_run_left_to_right(void)
{
    gui::PanelSlot slots[gui::MAX_PANELS];
    gui::compute_layout(3,
                        gui::LayoutOrientation::Horizontal,
                        gui::LayoutStyle::Strip,
                        slots);

    /**
     * Correct, and load-bearing: there is no hero on a strip, so
     * compute_prominence_ranks() puts every slot in one tier and priority
     * rank 0 lands leftmost. That is what the web form's priority ordering
     * should mean on this product. */
    for (uint8_t i = 0; i < 3; i++)
    {
        TEST_ASSERT_FALSE(gui::is_hero_slot(slots[i]));
    }

    uint8_t rank_of_slot[gui::MAX_PANELS];
    gui::compute_prominence_ranks(slots, 3, rank_of_slot);
    TEST_ASSERT_EQUAL_UINT8(0, rank_of_slot[0]);
    TEST_ASSERT_EQUAL_UINT8(1, rank_of_slot[1]);
    TEST_ASSERT_EQUAL_UINT8(2, rank_of_slot[2]);
}

void test_strip_count_one_is_still_a_hero(void)
{
    /**
     * The zoom view. A single full-screen panel is genuinely a hero and
     * must report as one, or it will not be given the largest font. */
    gui::PanelSlot slots[gui::MAX_PANELS];
    gui::compute_layout(1,
                        gui::LayoutOrientation::Horizontal,
                        gui::LayoutStyle::Strip,
                        slots);
    TEST_ASSERT_TRUE(gui::is_hero_slot(slots[0]));
}

void test_strip_vertical_is_a_true_transpose(void)
{
    const uint8_t divisors[] = { 1, 2, 3, 6 };
    for (uint8_t count : divisors)
    {
        gui::PanelSlot h[gui::MAX_PANELS];
        gui::PanelSlot v[gui::MAX_PANELS];
        gui::compute_layout(count,
                            gui::LayoutOrientation::Horizontal,
                            gui::LayoutStyle::Strip,
                            h);
        gui::compute_layout(count,
                            gui::LayoutOrientation::Vertical,
                            gui::LayoutStyle::Strip,
                            v);

        for (uint8_t i = 0; i < count; i++)
        {
            TEST_ASSERT_EQUAL_UINT8(h[i].row, v[i].col);
            TEST_ASSERT_EQUAL_UINT8(h[i].col, v[i].row);
            TEST_ASSERT_EQUAL_UINT8(h[i].rowspan, v[i].colspan);
            TEST_ASSERT_EQUAL_UINT8(h[i].colspan, v[i].rowspan);
        }
    }
}

/**
 * ======================================================================
 * gui::ZoomCycle -- the short-press cycle that replaces "promote to hero"
 *=====================================================================*/

void test_zoom_starts_on_the_overview()
{
    gui::ZoomCycle zoom;
    zoom.set_overview_count(3);

    TEST_ASSERT_FALSE(zoom.zoomed());
    TEST_ASSERT_EQUAL_UINT8(3, zoom.panel_count());
    TEST_ASSERT_EQUAL_UINT8(0, zoom.rank_offset());
}

void test_zoom_visits_every_rank_then_returns()
{
    gui::ZoomCycle zoom;
    zoom.set_overview_count(3);

    /**
     * Three signals means four positions: the overview plus one per
     * signal. Anything else and the gesture either skips a gauge or
     * needs pressing a different number of times than there are panels. */
    for (uint8_t rank = 0; rank < 3; rank++)
    {
        zoom.advance();
        TEST_ASSERT_TRUE(zoom.zoomed());
        TEST_ASSERT_EQUAL_UINT8(rank, zoom.zoomed_rank());
        TEST_ASSERT_EQUAL_UINT8(rank, zoom.rank_offset());
        /**
         * One panel, whichever rank it is showing -- which is exactly
         * what makes the zoom view compute_layout(1, ..., Strip) with an
         * offset rather than a layout of its own. */
        TEST_ASSERT_EQUAL_UINT8(1, zoom.panel_count());
    }

    zoom.advance();
    TEST_ASSERT_FALSE(zoom.zoomed());
    TEST_ASSERT_EQUAL_UINT8(3, zoom.panel_count());
    TEST_ASSERT_EQUAL_UINT8(0, zoom.rank_offset());
}

void test_zoom_wraps_forever()
{
    gui::ZoomCycle zoom;
    zoom.set_overview_count(3);

    /**
     * Two full laps. A cycle that stopped, or drifted, after a few
     * hundred presses would be a nuisance nobody could reproduce. */
    for (uint8_t lap = 0; lap < 2; lap++)
    {
        zoom.advance();
        TEST_ASSERT_EQUAL_UINT8(0, zoom.zoomed_rank());
        zoom.advance();
        zoom.advance();
        zoom.advance();
        TEST_ASSERT_FALSE(zoom.zoomed());
    }
}

void test_zoom_is_inert_with_a_single_panel_overview()
{
    gui::ZoomCycle zoom;
    zoom.set_overview_count(1);

    /**
     * Zooming into rank 0 from a one-panel overview is the same view
     * twice, so the cycle would have a step where the button visibly
     * does nothing. One panel is one position. */
    for (uint8_t i = 0; i < 5; i++)
    {
        zoom.advance();
        TEST_ASSERT_FALSE(zoom.zoomed());
        TEST_ASSERT_EQUAL_UINT8(1, zoom.panel_count());
    }
}

void test_zoom_returns_to_the_overview_when_the_count_changes()
{
    gui::ZoomCycle zoom;
    zoom.set_overview_count(3);
    zoom.advance();
    zoom.advance();
    TEST_ASSERT_EQUAL_UINT8(1, zoom.zoomed_rank());

    /**
     * The set being cycled through has changed; holding a zoom on what
     * used to be rank 1 is not a position worth preserving, and with a
     * shrinking count it may not exist any more. */
    zoom.set_overview_count(2);
    TEST_ASSERT_FALSE(zoom.zoomed());
    TEST_ASSERT_EQUAL_UINT8(2, zoom.panel_count());
}

void test_zoom_reset_returns_from_anywhere()
{
    gui::ZoomCycle zoom;
    zoom.set_overview_count(3);
    zoom.advance();
    zoom.advance();

    zoom.reset();
    TEST_ASSERT_FALSE(zoom.zoomed());
    TEST_ASSERT_EQUAL_UINT8(3, zoom.panel_count());

    /**
     * And the cycle carries on from the start rather than from where it
     * was interrupted. */
    zoom.advance();
    TEST_ASSERT_EQUAL_UINT8(0, zoom.zoomed_rank());
}

void test_zoom_clamps_an_overview_larger_than_the_panel_grid()
{
    gui::ZoomCycle zoom;
    zoom.set_overview_count(200);
    TEST_ASSERT_EQUAL_UINT8(gui::MAX_PANELS, zoom.panel_count());

    /**
     * Every zoomed position must be a rank InfoScreen::set_rank_offset()
     * can actually address, or the offset runs off priorities[]. */
    for (uint8_t i = 0; i < gui::MAX_PANELS; i++)
    {
        zoom.advance();
        TEST_ASSERT_TRUE(zoom.rank_offset() < gui::MAX_PANELS);
    }

    zoom.set_overview_count(0);
    TEST_ASSERT_EQUAL_UINT8(1, zoom.panel_count());
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_single_panel_spans_full_screen);
    RUN_TEST(test_two_panels_are_equal_full_height_columns);
    RUN_TEST(test_three_panels_are_hero_plus_stack);
    RUN_TEST(test_four_panels_are_equal_2x2);
    RUN_TEST(test_five_panels_matches_old_fixed_layout);
    RUN_TEST(test_six_panels_are_equal_2x3);
    RUN_TEST(test_horizontal_full_coverage_for_every_count);
    RUN_TEST(test_vertical_is_a_transpose_of_horizontal);
    RUN_TEST(test_five_panels_vertical_is_2x2_plus_wide_row);
    RUN_TEST(test_count_is_clamped_to_valid_range);
    RUN_TEST(test_hero_slot_extraction_matches_table);
    RUN_TEST(test_prominence_rank_count1);
    RUN_TEST(test_prominence_rank_count2);
    RUN_TEST(test_prominence_rank_count3);
    RUN_TEST(test_prominence_rank_count4);
    RUN_TEST(test_prominence_rank_count5);
    RUN_TEST(test_prominence_rank_count6);
    RUN_TEST(test_prominence_rank_matches_horizontal_under_vertical_transpose);
    RUN_TEST(test_prominence_rank_count5_vertical);
    RUN_TEST(test_grid_output_is_byte_identical_to_before_the_style_axis);
    RUN_TEST(test_strip_gives_equal_full_height_bands);
    RUN_TEST(test_strip_tiles_completely_for_every_divisor_of_the_band);
    RUN_TEST(test_strip_falls_through_to_grid_for_an_indivisible_count);
    RUN_TEST(test_strip_has_no_hero_so_ranks_run_left_to_right);
    RUN_TEST(test_strip_count_one_is_still_a_hero);
    RUN_TEST(test_strip_vertical_is_a_true_transpose);
    RUN_TEST(test_zoom_starts_on_the_overview);
    RUN_TEST(test_zoom_visits_every_rank_then_returns);
    RUN_TEST(test_zoom_wraps_forever);
    RUN_TEST(test_zoom_is_inert_with_a_single_panel_overview);
    RUN_TEST(test_zoom_returns_to_the_overview_when_the_count_changes);
    RUN_TEST(test_zoom_reset_returns_from_anywhere);
    RUN_TEST(test_zoom_clamps_an_overview_larger_than_the_panel_grid);
    return UNITY_END();
}
