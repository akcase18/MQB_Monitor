/**
 * @file test_main.cpp
 * @brief That the fonts a theme names actually fit the panels its metrics
 * describe.
 *
 * This exists because the build plan's own font recommendation was wrong.
 * §7.3 sized the gauge product's 3-up readout at jetbrains_mono_bold_46 on
 * the assumption that three characters was the worst case ("145", "9.8").
 * It is not: unit_decimals() gives Pressure two decimals in metric and one
 * in imperial, so oil pressure reads "10.34" bar or "150.0" psi -- five
 * characters, 138 px in bold_46, in a panel with 98 px of usable width.
 *
 * A clipped value is not something a code review catches and not something
 * a glance at the simulator reliably catches either, since it only shows up
 * once a signal happens to reach a wide enough number. So the arithmetic
 * is asserted here instead, against the real font data, and a future change
 * to a font or a metric fails a build rather than shipping a dashboard that
 * drops a digit at full scale.
 *
 * Run with `pio test -e simulator`.
 */
#include <unity.h>

#include "layout.hpp"
#include "signal_catalog.hpp"
#include "theme.hpp"

extern "C"
{
    /**
     * Same declarations theme.cpp makes. Named here so a font choice can
     * be asserted by identity, and so the "would the next size up have
     * fit?" check has something to measure. */
    extern const lv_font_t jetbrains_mono_bold_30;
    extern const lv_font_t jetbrains_mono_bold_46;
    extern const lv_font_t jetbrains_mono_bold_56;
    extern const lv_font_t jetbrains_mono_bold_96;
}

namespace
{
    /** Advance width of a digit, in whole pixels, from the font itself. */
    float digit_advance_px(const lv_font_t *font)
    {
        lv_font_glyph_dsc_t dsc;
        /**
         * '0' followed by '0' -- monospace, so any digit gives the same
         * answer and kerning is not a factor. */
        if (!lv_font_get_glyph_dsc(font, &dsc, '0', '0'))
        {
            return 0.0f;
        }
        return static_cast<float>(dsc.adv_w);
    }

    /**
     * Usable width inside one panel of a `count`-wide Strip layout, after
     * the screen margin, the gaps between panels, and the panel's own
     * border on both sides.
     */
    int strip_panel_usable_width(const gui::Metrics &m, uint8_t count)
    {
        int gaps = (count > 0) ? (count - 1) : 0;
        int inner
            = m.screen_width - (2 * m.screen_margin) - (gaps * m.panel_gap);
        int column = inner / count;
        return column - (2 * m.panel_border_width);
    }

    /**
     * The realistic worst case, and the reason bold_46 was rejected:
     * Pressure metric carries 2 decimals, so a two-digit bar reading is
     * "10.34". Imperial carries 1, so a three-digit psi reading is
     * "150.0". Both are five characters. A leading minus on a temperature
     * ("-40") is only three. */
    constexpr int WORST_CASE_CHARS = 5;

    /** Advance width of the unit tag's widest label, in pixels. */
    float widest_unit_px(const lv_font_t *font)
    {
        /**
         * Every label core::unit_label() can return. Measured rather than
         * assumed to be three characters, because "lb-ft" is five and
         * nothing stops a future unit type from being wider still. */
        static const core::UnitType TYPES[] = {
            core::UnitType::Degrees, core::UnitType::Percent,
            core::UnitType::Pressure, core::UnitType::RPM,
            core::UnitType::Speed,   core::UnitType::Temp,
            core::UnitType::Torque,  core::UnitType::Volume,
        };

        float widest = 0.0f;
        for (core::UnitType type : TYPES)
        {
            for (int metric = 0; metric < 2; metric++)
            {
                const char *label = core::unit_label(type, metric != 0);
                float       total = 0.0f;
                for (const char *c = label; *c != '\0'; c++)
                {
                    lv_font_glyph_dsc_t dsc;
                    /**
                     * Degrees and the temperature labels are multi-byte
                     * UTF-8; measuring per byte would over-count. Ask for
                     * the ASCII range only and approximate the rest with
                     * the font's own advance for 'C', which is what those
                     * labels are mostly made of. */
                    uint32_t ch = static_cast<uint8_t>(*c);
                    if (ch >= 0x80)
                    {
                        ch = 'C';
                    }
                    if (lv_font_get_glyph_dsc(font, &dsc, ch, 0))
                    {
                        total += static_cast<float>(dsc.adv_w);
                    }
                }
                if (total > widest)
                {
                    widest = total;
                }
            }
        }
        return widest;
    }
} // namespace

void test_unit_decimals_still_justify_the_five_character_budget(void)
{
    /**
     * If this ever changes, the budget below is wrong and the font choice
     * has to be revisited -- so tie the two together rather than leaving
     * WORST_CASE_CHARS as a number somebody has to remember why. */
    TEST_ASSERT_EQUAL_UINT8(
        2, core::unit_decimals(core::UnitType::Pressure, true));
    TEST_ASSERT_EQUAL_UINT8(
        1, core::unit_decimals(core::UnitType::Pressure, false));
    TEST_ASSERT_EQUAL_UINT8(0, core::unit_decimals(core::UnitType::Temp, true));
}

void test_gauge_three_up_readout_fits_its_panel(void)
{
    const gui::Metrics &m = gui::THEME_GAUGE.metrics;
    TEST_ASSERT_EQUAL_UINT16(320, m.screen_width);
    TEST_ASSERT_EQUAL_UINT16(172, m.screen_height);

    int usable = strip_panel_usable_width(m, 3);
    TEST_ASSERT_TRUE(usable > 0);

    float advance = digit_advance_px(gui::THEME_GAUGE.typography.data_sm);
    TEST_ASSERT_TRUE(advance > 0.0f);

    float needed = advance * WORST_CASE_CHARS;
    TEST_ASSERT_TRUE_MESSAGE(
        needed <= static_cast<float>(usable),
        "THEME_GAUGE.data_sm clips a 5-character value in a 3-up panel");

    /**
     * And the face actually chosen must not be gratuitously small either
     * -- a readout has to be legible at arm's length, so require that the
     * next size up genuinely would not have fit. */
    float bigger = digit_advance_px(&jetbrains_mono_bold_46);
    TEST_ASSERT_TRUE_MESSAGE(
        (bigger * WORST_CASE_CHARS) > static_cast<float>(usable),
        "bold_46 now fits -- reconsider data_sm rather than leaving it");
}

void test_gauge_zoom_readout_fits_a_full_width_panel(void)
{
    /**
     * The zoom view (LayoutStyle::Strip, count 1) is where bold_96 earns
     * its place and why it was retained. */
    const gui::Metrics &m = gui::THEME_GAUGE.metrics;

    int   usable = strip_panel_usable_width(m, 1);
    float needed = digit_advance_px(gui::THEME_GAUGE.typography.data_mega)
                   * WORST_CASE_CHARS;

    TEST_ASSERT_TRUE_MESSAGE(
        needed <= static_cast<float>(usable),
        "THEME_GAUGE.data_mega clips a 5-character value when zoomed");

    /**
     * The bar/gauge hero face shares the panel with a graphic, so it is a
     * step down -- but it still has to fit. */
    float hero = digit_advance_px(gui::THEME_GAUGE.typography.data_hero)
                 * WORST_CASE_CHARS;
    TEST_ASSERT_TRUE(hero <= static_cast<float>(usable));
}

void test_gauge_readouts_fit_vertically_too(void)
{
    /**
     * Width is the binding constraint, but a 172 px panel is short enough
     * that height is worth checking rather than assuming. Value plus
     * label plus both borders must clear the panel. */
    const gui::Metrics &m = gui::THEME_GAUGE.metrics;

    int panel_h
        = m.screen_height - (2 * m.screen_margin) - (2 * m.panel_border_width);

    /**
     * The unit tag is a third line on this product, not a tag beside the
     * value -- so it costs height, and on a 172px panel that is the
     * budget worth watching. Counted here rather than left implicit,
     * because the fix for the width problem moved the cost to this axis. */
    int unit_line = gui::THEME_GAUGE.metrics.unit_stacked
                        ? gui::THEME_GAUGE.typography.unit->line_height
                        : 0;

    int stacked = gui::THEME_GAUGE.typography.data_sm->line_height
                  + gui::THEME_GAUGE.typography.label_sm->line_height
                  + unit_line;
    TEST_ASSERT_TRUE_MESSAGE(
        stacked <= panel_h,
        "3-up value + unit + label is taller than the panel");

    int zoomed = gui::THEME_GAUGE.typography.data_mega->line_height
                 + gui::THEME_GAUGE.typography.label_sm->line_height
                 + unit_line;
    TEST_ASSERT_TRUE_MESSAGE(
        zoomed <= panel_h,
        "zoomed value + unit + label is taller than the panel");
}

void test_the_gauge_unit_tag_fits_beside_nothing(void)
{
    /**
     * The bug this test exists for: the fit check above measured the
     * value and stopped there, but a Digital panel draws the unit tag as
     * well. With the tag beside a centred value the pair overhangs the
     * centre line by half the value plus the whole tag, so "77.2" plus
     * "psi" ran off the edge of a 3-up column and rendered as "ps" --
     * visible on screen, invisible to every assertion in this file.
     *
     * THEME_GAUGE's answer is Metrics::unit_stacked, which puts the tag
     * on its own line and makes the constraint max(value, tag) rather
     * than their sum. So assert the rule that actually applies to each
     * theme, not one rule for both. */
    const gui::Metrics &m = gui::THEME_GAUGE.metrics;

    TEST_ASSERT_TRUE_MESSAGE(
        m.unit_stacked,
        "un-stacking the gauge tag puts it back off the panel edge");

    int   usable = strip_panel_usable_width(m, 3);
    float tag    = widest_unit_px(gui::THEME_GAUGE.typography.unit);
    TEST_ASSERT_TRUE(tag > 0.0f);

    /* Stacked: the tag has the column to itself. */
    TEST_ASSERT_TRUE_MESSAGE(tag <= static_cast<float>(usable),
                             "the unit tag alone is wider than a 3-up panel");

    /**
     * And the sum is what would have had to fit had it not been stacked
     * -- recorded so the reason stacking exists stays visible. */
    float value = digit_advance_px(gui::THEME_GAUGE.typography.data_sm)
                  * WORST_CASE_CHARS;
    TEST_ASSERT_TRUE_MESSAGE(
        (value + tag) > static_cast<float>(usable),
        "value + tag now fits side by side -- unit_stacked may be droppable");
}

void test_the_mqb_theme_is_untouched(void)
{
    /**
     * THEME_GAUGE is an addition, not an edit. The MQB product's geometry
     * and font ladder must be exactly what they were. */
    const gui::Theme &t = gui::THEME_DEFAULT;

    TEST_ASSERT_EQUAL_UINT16(480, t.metrics.screen_width);
    TEST_ASSERT_EQUAL_UINT16(320, t.metrics.screen_height);
    TEST_ASSERT_EQUAL_UINT8(10, t.metrics.panel_radius);
    TEST_ASSERT_EQUAL_UINT8(2, t.metrics.panel_border_width);
    TEST_ASSERT_EQUAL_UINT8(3, t.metrics.panel_gap);
    TEST_ASSERT_EQUAL_UINT8(3, t.metrics.screen_margin);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.45f, t.metrics.panel_height_sm_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.55f, t.metrics.panel_height_lg_pct);
    TEST_ASSERT_FALSE_MESSAGE(
        t.metrics.unit_stacked,
        "the CAN product's unit tag moved -- it has always sat beside the "
        "value, and its panels are wide enough for it");

    TEST_ASSERT_EQUAL_PTR(&jetbrains_mono_bold_30, t.typography.data_sm);
    TEST_ASSERT_EQUAL_PTR(&jetbrains_mono_bold_46, t.typography.data_lg);
    TEST_ASSERT_EQUAL_PTR(&jetbrains_mono_bold_56, t.typography.data_hero);
    TEST_ASSERT_EQUAL_PTR(&jetbrains_mono_bold_96, t.typography.data_mega);
}

void test_the_gauge_theme_keeps_the_mqb_palette(void)
{
    /**
     * The reskin here is geometry, not color -- stated in theme.cpp and
     * asserted so a later palette divergence is a deliberate edit. */
    TEST_ASSERT_EQUAL_UINT32(lv_color_to_u32(gui::THEME_DEFAULT.palette.bg),
                             lv_color_to_u32(gui::THEME_GAUGE.palette.bg));
    TEST_ASSERT_EQUAL_UINT32(
        lv_color_to_u32(gui::THEME_DEFAULT.palette.warn_hi),
        lv_color_to_u32(gui::THEME_GAUGE.palette.warn_hi));
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_unit_decimals_still_justify_the_five_character_budget);
    RUN_TEST(test_gauge_three_up_readout_fits_its_panel);
    RUN_TEST(test_gauge_zoom_readout_fits_a_full_width_panel);
    RUN_TEST(test_gauge_readouts_fit_vertically_too);
    RUN_TEST(test_the_gauge_unit_tag_fits_beside_nothing);
    RUN_TEST(test_the_mqb_theme_is_untouched);
    RUN_TEST(test_the_gauge_theme_keeps_the_mqb_palette);
    return UNITY_END();
}
