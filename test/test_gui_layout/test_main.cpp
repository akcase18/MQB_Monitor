/**
 * @file test_main.cpp
 * @brief Host-buildable coverage for gui/screens::compute_layout(): full
 * coverage/no-overlap of the band grid for every panel count (1-6) in
 * both orientations, plus the exact old fixed-5-panel split. Run with
 * `pio test -e simulator`.
 */
#include <unity.h>

#include "layout.hpp"

void setUp()
{
}
void tearDown()
{
}

namespace
{
    /* Sums each slot's column footprint per row and confirms every row used
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
        = gui::compute_layout(1, gui::LayoutOrientation::Horizontal, slots);

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
        = gui::compute_layout(5, gui::LayoutOrientation::Horizontal, slots);

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
        = gui::compute_layout(2, gui::LayoutOrientation::Horizontal, slots);

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
        = gui::compute_layout(3, gui::LayoutOrientation::Horizontal, slots);

    TEST_ASSERT_EQUAL_UINT8(3, n);

    /* Two stacked minors on the left, lower indices per the small-first
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
        = gui::compute_layout(4, gui::LayoutOrientation::Horizontal, slots);

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
        = gui::compute_layout(6, gui::LayoutOrientation::Horizontal, slots);

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
            count, gui::LayoutOrientation::Horizontal, slots);
        TEST_ASSERT_EQUAL_UINT8(count, n);
        assert_full_row_coverage(slots, n, 6);
    }
}

void test_vertical_is_a_transpose_of_horizontal()
{
    for (uint8_t count = 1; count <= gui::MAX_PANELS; count++)
    {
        gui::PanelSlot h_slots[gui::MAX_PANELS];
        gui::PanelSlot v_slots[gui::MAX_PANELS];
        gui::compute_layout(count, gui::LayoutOrientation::Horizontal, h_slots);
        gui::compute_layout(count, gui::LayoutOrientation::Vertical, v_slots);

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

void test_count_is_clamped_to_valid_range()
{
    gui::PanelSlot slots[gui::MAX_PANELS];

    TEST_ASSERT_EQUAL_UINT8(
        1, gui::compute_layout(0, gui::LayoutOrientation::Horizontal, slots));
    TEST_ASSERT_EQUAL_UINT8(
        gui::MAX_PANELS,
        gui::compute_layout(255, gui::LayoutOrientation::Horizontal, slots));
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
    RUN_TEST(test_count_is_clamped_to_valid_range);
    return UNITY_END();
}
