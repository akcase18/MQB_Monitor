/**
 * @file test_main.cpp
 * @brief gui::VehicleBinding -- the one bridge between core:: and
 * gui/widgets, and specifically where a panel's warning highlight comes
 * from.
 *
 * The gauge product needs a warning that has *happened* to stay lit after
 * the value has come back inside, which the stateless evaluate_warning()
 * cannot express. The fix was an optional core::WarningLatch the binding
 * reads rather than drives (see set_warning_latch), and this suite pins
 * both halves: that the default path is unchanged for the CAN product, and
 * that reading through the latch never advances it.
 *
 * Run with `pio test -e simulator`.
 */
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "config_store.hpp"
#include "signal_store.hpp"
#include "vehicle_binding.hpp"
#include "warning_latch.hpp"

namespace
{
    class FakeConfigStore : public IConfigStore
    {
        public:
            static constexpr size_t CAPACITY = 2048;

            FakeConfigStore()
            {
                memset(bytes_, 0xFF, sizeof(bytes_));
            }

            bool read(uint32_t offset, void *buf, size_t len) override
            {
                if (offset + len > CAPACITY)
                {
                    return false;
                }
                memcpy(buf, bytes_ + offset, len);
                return true;
            }

            bool write(uint32_t offset, const void *buf, size_t len) override
            {
                if (offset + len > CAPACITY)
                {
                    return false;
                }
                memcpy(bytes_ + offset, buf, len);
                return true;
            }

            size_t capacity() const override
            {
                return CAPACITY;
            }

        private:
            uint8_t bytes_[CAPACITY];
    };

    /**
     * Rank 0's signal, whatever the compiled catalog puts there -- the
     * suite is about the warning path, not about any particular product's
     * signal list, and this file builds against both. */
    struct Fixture
    {
            FakeConfigStore     backing;
            core::ConfigStore   config { backing };
            core::SignalStore   store;
            gui::VehicleBinding binding { store, config };

            uint16_t signal_id = 0;
            int      idx       = 0;

            Fixture()
            {
                config.load(); /* unrecognised magic -> factory defaults */

                signal_id = config.get().priorities[0].signal_id;
                idx       = core::signal_catalog_find(signal_id);

                /**
                 * Metric, so the thresholds below are in the same unit the
                 * store holds and no conversion is in the way. */
                config.get().priorities[0].units = core::UnitSystem::Metric;

                /**
                 * Warn below 1.5, no high bound -- low oil pressure in
                 * shape, whichever signal rank 0 actually holds. */
                core::WarningThresholds &warn = config.get().signals[idx].warn;
                warn.use_lo                   = true;
                warn.lo                       = 1.5f;
                warn.use_hi                   = false;
            }
    };
} // namespace

void setUp()
{
}
void tearDown()
{
}

void test_without_a_latch_the_highlight_is_the_live_evaluation(void)
{
    /**
     * The CAN product's path, and it must not have changed: no latch set,
     * so the highlight follows the value in and out with no memory. */
    Fixture f;

    f.store.set_at(f.idx, 4.0f, 1000);
    TEST_ASSERT_FALSE(f.binding.build_view_model(0, false, false).warn_lo_active);

    f.store.set_at(f.idx, 0.9f, 1100);
    TEST_ASSERT_TRUE(f.binding.build_view_model(0, false, false).warn_lo_active);

    f.store.set_at(f.idx, 4.0f, 1200);
    TEST_ASSERT_FALSE(f.binding.build_view_model(0, false, false).warn_lo_active);
}

void test_with_a_latch_the_highlight_survives_the_value_recovering(void)
{
    Fixture f;

    core::WarningLatch latch;
    latch.reset();
    f.binding.set_warning_latch(&latch);

    const core::WarningThresholds &warn = f.config.get().signals[f.idx].warn;

    f.store.set_at(f.idx, 4.0f, 1000);
    latch.update(f.idx, f.store.get_at(f.idx), warn, 1000);
    TEST_ASSERT_FALSE(f.binding.build_view_model(0, false, false).warn_lo_active);

    /**
     * One dip, then gone again -- the case a 20 Hz refresh would miss
     * entirely without the latch. */
    f.store.set_at(f.idx, 0.9f, 1100);
    latch.update(f.idx, f.store.get_at(f.idx), warn, 1100);

    f.store.set_at(f.idx, 4.0f, 1200);
    latch.update(f.idx, f.store.get_at(f.idx), warn, 1200);

    gui::PanelViewModel vm = f.binding.build_view_model(0, false, false);
    TEST_ASSERT_TRUE(vm.warn_lo_active);
    /**
     * The value shown is the current one, not the one that tripped it --
     * the highlight is a memory, the reading never is. Formatted the same
     * way the binding does rather than written out, since how many
     * decimals rank 0's signal gets is a property of whichever catalog
     * this suite was compiled against. */
    char expected[gui::PANEL_VALUE_LEN];
    snprintf(expected,
             sizeof(expected),
             "%.*f",
             core::unit_decimals(core::SIGNAL_CATALOG[f.idx].unit_type, true),
             4.0);
    TEST_ASSERT_EQUAL_STRING(expected, vm.value_str);

    latch.acknowledge();
    TEST_ASSERT_FALSE(f.binding.build_view_model(0, false, false).warn_lo_active);
}

void test_building_a_view_model_does_not_drive_the_latch(void)
{
    /**
     * The whole reason the binding reads rather than updates. The gauge
     * product's zoom view draws one panel of three; if drawing were what
     * advanced qualification, the two signals off screen would stop
     * qualifying and a warning would depend on where the driver was
     * looking. */
    Fixture f;

    core::WarningLatch latch;
    latch.reset();
    /**
     * Zero, deliberately: the latch would raise on the very first sample
     * it was given. So if drawing fed it one, the first draw would light
     * the panel up. Any non-zero qualify time makes this test vacuous --
     * it then passes against a binding that *does* drive the latch,
     * simply because the timestamp it would pass in never advances. That
     * is exactly how the first version of this test let that mutation
     * through. */
    latch.set_qualify_ms(0);
    f.binding.set_warning_latch(&latch);

    f.store.set_at(f.idx, 0.9f, 1000);

    /**
     * Drawn over and over with nobody ticking the latch. Nothing may
     * ever light up: a panel nobody is looking at has to qualify on the
     * same schedule as one in front of the driver. */
    for (uint32_t t = 0; t < 100; t++)
    {
        TEST_ASSERT_FALSE(
            f.binding.build_view_model(0, false, false).warn_lo_active);
    }
    TEST_ASSERT_FALSE(latch.any_held());
}

void test_a_latched_signal_with_no_data_still_blanks_its_reading(void)
{
    /**
     * A plausibility failure declines to write, so has_data stays false
     * and the panel blanks -- and it must blank even while the latch is
     * still showing the warning that fired before the wire came loose.
     * Two independent things: what the panel reads, and what it
     * highlights. */
    Fixture f;

    core::WarningLatch latch;
    latch.reset();
    f.binding.set_warning_latch(&latch);

    const core::WarningThresholds &warn = f.config.get().signals[f.idx].warn;

    f.store.set_at(f.idx, 0.9f, 1000);
    latch.update(f.idx, f.store.get_at(f.idx), warn, 1000);
    TEST_ASSERT_TRUE(latch.any_held());

    /**
     * A fresh store stands in for "this signal has never been written",
     * which is exactly the state a blanked channel is left in. */
    core::SignalStore   dark;
    gui::VehicleBinding dark_binding(dark, f.config);
    dark_binding.set_warning_latch(&latch);

    gui::PanelViewModel vm = dark_binding.build_view_model(0, false, false);
    TEST_ASSERT_EQUAL_STRING("--", vm.value_str);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_without_a_latch_the_highlight_is_the_live_evaluation);
    RUN_TEST(test_with_a_latch_the_highlight_survives_the_value_recovering);
    RUN_TEST(test_building_a_view_model_does_not_drive_the_latch);
    RUN_TEST(test_a_latched_signal_with_no_data_still_blanks_its_reading);
    return UNITY_END();
}
