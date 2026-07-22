/**
 * @file test_main.cpp
 * @brief Host-buildable coverage for core/config: defaults on blank
 * storage, full round trip, partial (panel/signal) writes, unknown-version
 * migration fallback, and core/vehicle's evaluate_warning(). Run with
 * `pio test -e simulator`.
 */
#include <string.h>
#include <unity.h>

#include "config_store.hpp"
#include "warning.hpp"

namespace
{
    /* In-memory IConfigStore test double — avoids touching the real
     * simulator's sim_config.bin file, so tests stay hermetic. */
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
} // namespace

void setUp()
{
}
void tearDown()
{
}

void test_defaults_on_blank_storage()
{
    FakeConfigStore   backing; // starts all-0xFF, like erased flash
    core::ConfigStore store(backing);

    store.load();

    const core::Config &cfg = store.get();
    TEST_ASSERT_EQUAL_UINT16(core::ConfigStore::MAGIC_NUMBER, cfg.header.magic);
    TEST_ASSERT_EQUAL_UINT8(core::ConfigStore::CURRENT_VERSION,
                            cfg.header.version);
    TEST_ASSERT_EQUAL_UINT8(5, cfg.settings.num_panels);
    TEST_ASSERT_TRUE(core::LayoutOrientation::Horizontal
                     == cfg.settings.orientation);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.priorities[0].signal_id);  // Coolant Temp
    TEST_ASSERT_EQUAL_UINT16(12, cfg.priorities[1].signal_id); // Throttle Pos
    TEST_ASSERT_EQUAL_UINT16(
        8, cfg.priorities[5].signal_id); // Engine Torque (inactive rank)

    for (uint8_t i = 0; i < core::MAX_PANELS; i++)
    {
        TEST_ASSERT_TRUE(core::DisplayMode::Digital
                         == cfg.priorities[i].display_mode);
    }
}

void test_display_mode_round_trips_via_save_priority()
{
    FakeConfigStore backing;

    core::ConfigStore store(backing);
    store.load();

    store.get().priorities[2].display_mode = core::DisplayMode::Gauge;
    store.save_priority(2);

    core::ConfigStore store2(backing);
    store2.load();

    TEST_ASSERT_TRUE(core::DisplayMode::Gauge
                     == store2.get().priorities[2].display_mode);
    TEST_ASSERT_TRUE(core::DisplayMode::Digital
                     == store2.get().priorities[0].display_mode); // untouched
}

void test_load_round_trips_after_modification()
{
    FakeConfigStore backing;

    {
        core::ConfigStore store(backing);
        store.load(); // seeds defaults + saves them

        core::Config &cfg           = store.get();
        cfg.settings.day_brightness = 42;
        cfg.priorities[2].signal_id = 99;
        store.save();
    }

    /* Fresh ConfigStore instance over the same backing bytes. */
    core::ConfigStore store2(backing);
    store2.load();

    TEST_ASSERT_EQUAL_UINT8(42, store2.get().settings.day_brightness);
    TEST_ASSERT_EQUAL_UINT16(99, store2.get().priorities[2].signal_id);
}

void test_save_priority_only_writes_that_rank()
{
    FakeConfigStore backing;

    core::ConfigStore store(backing);
    store.load(); // defaults + full save

    store.get().priorities[3].signal_id = 7;
    store.save_priority(3); // partial write, no full save()

    core::ConfigStore store2(backing);
    store2.load();

    TEST_ASSERT_EQUAL_UINT16(7, store2.get().priorities[3].signal_id);
    TEST_ASSERT_EQUAL_UINT16(0,
                             store2.get().priorities[0].signal_id); // untouched
}

void test_save_signal_only_writes_that_slot()
{
    FakeConfigStore backing;

    core::ConfigStore store(backing);
    store.load();

    core::WarningThresholds thresholds { 10.0f, 90.0f, true, true };
    store.get().signals[6].warn = thresholds; // Actual Intake Pressure
    store.save_signal(6);

    core::ConfigStore store2(backing);
    store2.load();

    TEST_ASSERT_EQUAL_FLOAT(10.0f, store2.get().signals[6].warn.lo);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, store2.get().signals[6].warn.hi);
    TEST_ASSERT_TRUE(store2.get().signals[6].warn.use_lo);
    TEST_ASSERT_TRUE(store2.get().signals[6].warn.use_hi);
    TEST_ASSERT_FALSE(store2.get().signals[0].warn.use_lo); // untouched
}

void test_custom_range_defaults_off_and_round_trips_via_save_signal()
{
    FakeConfigStore backing;

    core::ConfigStore store(backing);
    store.load();

    for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
    {
        TEST_ASSERT_FALSE(store.get().signals[i].use_custom_range);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, store.get().signals[i].custom_min);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, store.get().signals[i].custom_max);
    }

    store.get().signals[0].use_custom_range = true;
    store.get().signals[0].custom_min       = 0.0f;
    store.get().signals[0].custom_max       = 50.0f;
    store.save_signal(0);

    core::ConfigStore store2(backing);
    store2.load();

    TEST_ASSERT_TRUE(store2.get().signals[0].use_custom_range);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, store2.get().signals[0].custom_min);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, store2.get().signals[0].custom_max);
    TEST_ASSERT_FALSE(store2.get().signals[1].use_custom_range); // untouched
}

void test_unrecognized_version_resets_to_defaults()
{
    FakeConfigStore backing;

    /* Craft a header with a valid magic but an unrecognized (older)
     * version, and garbage settings behind it. */
    core::ConfigHeader header { core::ConfigStore::MAGIC_NUMBER, 0 };
    backing.write(0, &header, sizeof(header));

    core::ConfigStore store(backing);
    store.load();

    TEST_ASSERT_EQUAL_UINT8(core::ConfigStore::CURRENT_VERSION,
                            store.get().header.version);
    TEST_ASSERT_EQUAL_UINT8(5, store.get().settings.num_panels);
}

void test_evaluate_warning()
{
    core::WarningThresholds thresholds { 10.0f, 90.0f, true, true };

    core::SignalValue  no_data { 0.0f, false, 0 };
    core::WarningState state = core::evaluate_warning(no_data, thresholds);
    TEST_ASSERT_FALSE(state.lo_active);
    TEST_ASSERT_FALSE(state.hi_active);

    core::SignalValue mid { 50.0f, true, 0 };
    state = core::evaluate_warning(mid, thresholds);
    TEST_ASSERT_FALSE(state.lo_active);
    TEST_ASSERT_FALSE(state.hi_active);

    core::SignalValue low { 5.0f, true, 0 };
    state = core::evaluate_warning(low, thresholds);
    TEST_ASSERT_TRUE(state.lo_active);
    TEST_ASSERT_FALSE(state.hi_active);

    core::SignalValue high { 95.0f, true, 0 };
    state = core::evaluate_warning(high, thresholds);
    TEST_ASSERT_FALSE(state.lo_active);
    TEST_ASSERT_TRUE(state.hi_active);

    core::WarningThresholds disabled { 10.0f, 90.0f, false, false };
    state = core::evaluate_warning(low, disabled);
    TEST_ASSERT_FALSE(state.lo_active);
    TEST_ASSERT_FALSE(state.hi_active);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_defaults_on_blank_storage);
    RUN_TEST(test_display_mode_round_trips_via_save_priority);
    RUN_TEST(test_load_round_trips_after_modification);
    RUN_TEST(test_save_priority_only_writes_that_rank);
    RUN_TEST(test_save_signal_only_writes_that_slot);
    RUN_TEST(test_custom_range_defaults_off_and_round_trips_via_save_signal);
    RUN_TEST(test_unrecognized_version_resets_to_defaults);
    RUN_TEST(test_evaluate_warning);
    return UNITY_END();
}
