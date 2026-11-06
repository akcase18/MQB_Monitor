/**
 * @file test_main.cpp
 * @brief Host-buildable coverage for core/config: defaults on blank
 * storage, full round trip, partial (panel/signal) writes, unknown-version
 * migration fallback, v6-v9 configs carried across to v10, the log
 * profile (round trip, unoffered and repeated ids dropped), signal records
 * found by id after the catalog changes underneath them, a panel on a
 * signal this build doesn't offer reset to its default, a newer version left alone after a rollback (and
 * replaced whole, never patched, by the first save), and core/vehicle's
 * evaluate_warning(). Run with
 * `pio test -e simulator`.
 */
#include <stddef.h>
#include <string.h>
#include <unity.h>

#include "auto_brightness.hpp"
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
        cfg.settings.brightness_mode = core::BrightnessMode::Auto;
        cfg.settings.auto_brightness_pct = 85;
        cfg.priorities[2].signal_id = core::SIGNAL_CATALOG[3].id;
        store.save();
    }

    /* Fresh ConfigStore instance over the same backing bytes. */
    core::ConfigStore store2(backing);
    store2.load();

    TEST_ASSERT_EQUAL_UINT8(42, store2.get().settings.day_brightness);
    TEST_ASSERT_TRUE(core::BrightnessMode::Auto
                     == store2.get().settings.brightness_mode);
    TEST_ASSERT_EQUAL_UINT8(85, store2.get().settings.auto_brightness_pct);
    TEST_ASSERT_EQUAL_UINT16(core::SIGNAL_CATALOG[3].id,
                             store2.get().priorities[2].signal_id);
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
    store.get().signals[6].warn = thresholds; // Int Pres Act. (signal id 22)
    store.save_signal(22);

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

void test_newer_version_loads_defaults_without_overwriting()
{
    FakeConfigStore backing;

    /* Settings saved by newer firmware, then the board rolled back to
     * this one: a layout this build doesn't know, behind a valid magic. */
    core::ConfigHeader header {
        core::ConfigStore::MAGIC_NUMBER,
        static_cast<uint8_t>(core::ConfigStore::CURRENT_VERSION + 1)
    };
    backing.write(0, &header, sizeof(header));
    uint8_t future_settings = 77;
    backing.write(sizeof(header), &future_settings, 1);

    core::ConfigStore store(backing);
    store.load();
    TEST_ASSERT_EQUAL_UINT8(5, store.get().settings.num_panels);

    /* Still there for the newer firmware, should it be reinstalled. */
    core::ConfigHeader stored {};
    uint8_t            stored_settings = 0;
    backing.read(0, &stored, sizeof(stored));
    backing.read(sizeof(stored), &stored_settings, 1);
    TEST_ASSERT_EQUAL_UINT8(core::ConfigStore::CURRENT_VERSION + 1,
                            stored.version);
    TEST_ASSERT_EQUAL_UINT8(77, stored_settings);
}

void test_partial_save_over_newer_version_writes_whole_config()
{
    FakeConfigStore backing;

    core::ConfigHeader header {
        core::ConfigStore::MAGIC_NUMBER,
        static_cast<uint8_t>(core::ConfigStore::CURRENT_VERSION + 1)
    };
    backing.write(0, &header, sizeof(header));

    core::ConfigStore store(backing);
    store.load();

    /* A slot write alone would leave the newer header in place over
     * this layout's bytes; it has to become a full save instead. */
    store.get().priorities[3].signal_id = 7;
    store.save_priority(3);

    core::ConfigHeader stored {};
    backing.read(0, &stored, sizeof(stored));
    TEST_ASSERT_EQUAL_UINT8(core::ConfigStore::CURRENT_VERSION,
                            stored.version);

    core::ConfigStore store2(backing);
    store2.load();
    TEST_ASSERT_EQUAL_UINT16(7, store2.get().priorities[3].signal_id);
    TEST_ASSERT_EQUAL_UINT8(5, store2.get().settings.num_panels);

    /* Once it's this layout, partial writes are partial again. */
    store2.get().signals[6].warn.use_hi = true;
    store2.save_signal(22);
    core::ConfigStore store3(backing);
    store3.load();
    TEST_ASSERT_TRUE(store3.get().signals[6].warn.use_hi);
}

/**
 * Writes a v6-v8 head: the header, the 16-byte Settings those versions had
 * (a prefix of the current one), and the priorities at byte 20. Their
 * signal records start at byte 92.
 */
void write_old_head(FakeConfigStore                &backing,
                    uint8_t                         version,
                    const core::Settings           &settings,
                    const core::PrioritizedSignal  *priorities)
{
    core::ConfigHeader header { core::ConfigStore::MAGIC_NUMBER, version };
    backing.write(0, &header, sizeof(header));
    backing.write(4, &settings, 16);
    backing.write(20,
                  priorities,
                  sizeof(core::PrioritizedSignal) * core::MAX_PANELS);
}

void test_v6_config_survives_the_upgrade()
{
    FakeConfigStore backing;

    /**
     * A v6 blob as a dash in the field holds it: the old head, then 22
     * positional 24-byte records. Catalog position 6 is Int Pres Act.
     * (id 22), position 0 is Cool Temp (id 0).
     */
    core::Config v6 {};
    v6.settings.day_brightness   = 61;
    v6.settings.num_panels       = 3;
    v6.settings.accent_color     = 0x123456;
    v6.priorities[0].signal_id   = 22;
    v6.priorities[0].display_mode = core::DisplayMode::Gauge;
    /* v8's brightness_mode byte was padding in v6: anything at all. */
    v6.settings.brightness_mode = static_cast<core::BrightnessMode>(0xCD);
    write_old_head(backing, 6, v6.settings, v6.priorities);

    for (uint8_t i = 0; i < 22; i++)
    {
        core::SignalConfig rec {};
        rec.warn = { static_cast<float>(i), static_cast<float>(100 + i),
                     i == 6, i == 0 };
        backing.write(92 + i * 24u, &rec, 24);
    }

    core::ConfigStore store(backing);
    store.load();

    const core::Config &cfg = store.get();
    TEST_ASSERT_EQUAL_UINT8(core::ConfigStore::CURRENT_VERSION,
                            cfg.header.version);
    TEST_ASSERT_EQUAL_UINT8(61, cfg.settings.day_brightness);
    TEST_ASSERT_EQUAL_UINT8(3, cfg.settings.num_panels);
    TEST_ASSERT_EQUAL_HEX32(0x123456, cfg.settings.accent_color);
    TEST_ASSERT_EQUAL_UINT16(22, cfg.priorities[0].signal_id);
    TEST_ASSERT_TRUE(core::DisplayMode::Gauge == cfg.priorities[0].display_mode);
    TEST_ASSERT_TRUE(core::BrightnessMode::Day == cfg.settings.brightness_mode);
    TEST_ASSERT_EQUAL_UINT8(core::AUTO_BRIGHTNESS_NEUTRAL,
                            cfg.settings.auto_brightness_pct);

    for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
    {
        TEST_ASSERT_EQUAL_UINT16(core::SIGNAL_CATALOG[i].id,
                                 cfg.signals[i].signal_id);
    }
    int int_pres = core::signal_catalog_find(22);
    TEST_ASSERT_EQUAL_FLOAT(6.0f, cfg.signals[int_pres].warn.lo);
    TEST_ASSERT_EQUAL_FLOAT(106.0f, cfg.signals[int_pres].warn.hi);
    TEST_ASSERT_TRUE(cfg.signals[int_pres].warn.use_lo);
    int cool = core::signal_catalog_find(0);
    TEST_ASSERT_TRUE(cfg.signals[cool].warn.use_hi);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, cfg.signals[cool].warn.hi);

    /**
     * And it was written back current, so the next boot loads it directly.
     */
    core::ConfigStore store2(backing);
    store2.load();
    TEST_ASSERT_EQUAL_FLOAT(106.0f, store2.get().signals[int_pres].warn.hi);
    TEST_ASSERT_EQUAL_UINT8(61, store2.get().settings.day_brightness);
}

/**
 * A v7 or v8 blob: the old head, then the 64 id-tagged records at byte
 * 92. Night 33, rank 1 on Int Pres Act., Cool Temp's low warning at 5,
 * and `mode` in brightness_mode's byte.
 */
void write_v7_or_v8(FakeConfigStore &backing, uint8_t version, uint8_t mode)
{
    core::Config old {};
    {
        core::ConfigStore store(backing);
        store.load();
        old = store.get();
    }
    old.settings.night_brightness = 33;
    old.settings.brightness_mode  = static_cast<core::BrightnessMode>(mode);
    old.priorities[1].signal_id   = 22;
    int cool                      = core::signal_catalog_find(0);
    old.signals[cool].warn        = { 5.0f, 50.0f, true, false };
    write_old_head(backing, version, old.settings, old.priorities);
    backing.write(92, old.signals, sizeof(old.signals));
}

/**
 * What both a v7 and a v8 blob from write_v7_or_v8() load as, before and
 * after the write-back.
 */
void check_v7_or_v8_loaded(FakeConfigStore &backing, core::BrightnessMode mode)
{
    for (int boot = 0; boot < 2; boot++)
    {
        core::ConfigStore store(backing);
        store.load();

        const core::Config &cfg  = store.get();
        int                 cool = core::signal_catalog_find(0);
        TEST_ASSERT_EQUAL_UINT8(core::ConfigStore::CURRENT_VERSION,
                                cfg.header.version);
        TEST_ASSERT_TRUE(mode == cfg.settings.brightness_mode);
        TEST_ASSERT_EQUAL_UINT8(core::AUTO_BRIGHTNESS_NEUTRAL,
                                cfg.settings.auto_brightness_pct);
        TEST_ASSERT_EQUAL_UINT8(33, cfg.settings.night_brightness);
        TEST_ASSERT_EQUAL_UINT16(22, cfg.priorities[1].signal_id);
        TEST_ASSERT_EQUAL_FLOAT(5.0f, cfg.signals[cool].warn.lo);
        TEST_ASSERT_TRUE(cfg.signals[cool].warn.use_lo);
    }
}

void test_v7_config_survives_the_upgrade()
{
    FakeConfigStore backing;

    /**
     * brightness_mode's byte was padding in v7: junk becomes Day.
     */
    write_v7_or_v8(backing, 7, 0xAB);
    check_v7_or_v8_loaded(backing, core::BrightnessMode::Day);
}

void test_v8_config_survives_the_upgrade()
{
    FakeConfigStore backing;

    /**
     * v8 had the mode for real: it's kept.
     */
    write_v7_or_v8(
        backing, 8, static_cast<uint8_t>(core::BrightnessMode::Auto));
    check_v7_or_v8_loaded(backing, core::BrightnessMode::Auto);
}

void test_v9_config_survives_the_upgrade()
{
    FakeConfigStore backing;

    /**
     * A v9 blob is the current layout up to the end of signals[]. Junk in
     * log_source's byte (padding in v9) and past the v9 end (whatever the
     * store held) must not leak into the upgraded config.
     */
    core::Config old {};
    {
        core::ConfigStore store(backing);
        store.load();
        old = store.get();
    }
    old.header.version                = 9;
    old.settings.night_brightness     = 44;
    old.settings.brightness_mode      = core::BrightnessMode::Auto;
    old.settings.auto_brightness_pct  = 70;
    old.priorities[2].signal_id       = 22;
    int cool                          = core::signal_catalog_find(0);
    old.signals[cool].warn            = { 3.0f, 60.0f, true, true };
    backing.write(0, &old, offsetof(core::Config, log_count));
    uint8_t junk = 0xAB;
    backing.write(offsetof(core::Config, settings)
                      + offsetof(core::Settings, log_source),
                  &junk,
                  1);
    uint8_t tail[sizeof(core::Config) - offsetof(core::Config, log_count)];
    memset(tail, 0x07, sizeof(tail));
    backing.write(offsetof(core::Config, log_count), tail, sizeof(tail));

    for (int boot = 0; boot < 2; boot++)
    {
        core::ConfigStore store(backing);
        store.load();

        const core::Config &cfg = store.get();
        TEST_ASSERT_EQUAL_UINT8(core::ConfigStore::CURRENT_VERSION,
                                cfg.header.version);
        TEST_ASSERT_EQUAL_UINT8(44, cfg.settings.night_brightness);
        TEST_ASSERT_TRUE(core::BrightnessMode::Auto
                         == cfg.settings.brightness_mode);
        TEST_ASSERT_EQUAL_UINT8(70, cfg.settings.auto_brightness_pct);
        TEST_ASSERT_EQUAL_UINT16(22, cfg.priorities[2].signal_id);
        TEST_ASSERT_EQUAL_FLOAT(3.0f, cfg.signals[cool].warn.lo);
        TEST_ASSERT_TRUE(cfg.signals[cool].warn.use_hi);
        TEST_ASSERT_TRUE(core::LogSource::Screen == cfg.settings.log_source);
        TEST_ASSERT_EQUAL_UINT8(0, cfg.log_count);
        TEST_ASSERT_EQUAL_UINT16(core::NO_SIGNAL_ID, cfg.log_signals[0]);
    }
}

void test_log_profile_round_trips()
{
    FakeConfigStore backing;
    {
        core::ConfigStore store(backing);
        store.load();
        TEST_ASSERT_EQUAL_UINT8(0, store.get().log_count);
        TEST_ASSERT_TRUE(core::LogSource::Screen
                         == store.get().settings.log_source);

        core::Config &cfg         = store.get();
        cfg.settings.log_source   = core::LogSource::Profile;
        cfg.log_count             = 3;
        cfg.log_signals[0]        = 22;
        cfg.log_signals[1]        = 7;
        cfg.log_signals[2]        = core::SIGNAL_CATALOG[0].id;
        store.save();
    }

    core::ConfigStore store(backing);
    store.load();
    const core::Config &cfg = store.get();
    TEST_ASSERT_TRUE(core::LogSource::Profile == cfg.settings.log_source);
    TEST_ASSERT_EQUAL_UINT8(3, cfg.log_count);
    TEST_ASSERT_EQUAL_UINT16(22, cfg.log_signals[0]);
    TEST_ASSERT_EQUAL_UINT16(7, cfg.log_signals[1]);
    TEST_ASSERT_EQUAL_UINT16(core::SIGNAL_CATALOG[0].id, cfg.log_signals[2]);
}

void test_log_profile_drops_unoffered_and_repeated_ids()
{
    FakeConfigStore backing;
    {
        core::ConfigStore store(backing);
        store.load();
        core::Config &cfg       = store.get();
        cfg.settings.log_source = static_cast<core::LogSource>(9);
        /**
         * An out-of-range count is clamped before anything is read.
         */
        cfg.log_count      = 40;
        cfg.log_signals[0] = 22;
        cfg.log_signals[1] = 999; /* not in any catalog */
        cfg.log_signals[2] = 7;
        cfg.log_signals[3] = 22; /* repeat */
        cfg.log_signals[4] = core::NO_SIGNAL_ID;
        for (uint8_t i = 5; i < core::MAX_LOG_SIGNALS; i++)
        {
            cfg.log_signals[i] = 999;
        }
        store.save();
    }

    core::ConfigStore store(backing);
    store.load();
    const core::Config &cfg = store.get();
    TEST_ASSERT_TRUE(core::LogSource::Screen == cfg.settings.log_source);
    TEST_ASSERT_EQUAL_UINT8(2, cfg.log_count);
    TEST_ASSERT_EQUAL_UINT16(22, cfg.log_signals[0]);
    TEST_ASSERT_EQUAL_UINT16(7, cfg.log_signals[1]);
    for (uint8_t i = 2; i < core::MAX_LOG_SIGNALS; i++)
    {
        TEST_ASSERT_EQUAL_UINT16(core::NO_SIGNAL_ID, cfg.log_signals[i]);
    }
}

void test_out_of_range_brightness_settings_load_as_defaults()
{
    FakeConfigStore backing;

    {
        core::ConfigStore store(backing);
        store.load();
        store.get().settings.brightness_mode
            = static_cast<core::BrightnessMode>(7);
        store.get().settings.auto_brightness_pct = 180;
        store.save();
    }

    core::ConfigStore store(backing);
    store.load();
    TEST_ASSERT_TRUE(core::BrightnessMode::Day
                     == store.get().settings.brightness_mode);
    TEST_ASSERT_EQUAL_UINT8(core::AUTO_BRIGHTNESS_NEUTRAL,
                            store.get().settings.auto_brightness_pct);
}

void test_panel_on_a_signal_not_offered_resets_to_default()
{
    FakeConfigStore backing;

    uint16_t defaults[core::MAX_PANELS];
    {
        core::ConfigStore store(backing);
        store.load();
        for (uint8_t r = 0; r < core::MAX_PANELS; r++)
        {
            defaults[r] = store.get().priorities[r].signal_id;
        }

        /**
         * Rank 1 on an internal signal (one demoted from active, as Dim
         * Ctrl was), rank 2 on one no longer in the registry at all, rank
         * 0 on a real one that has to stay put.
         */
        core::Config &cfg           = store.get();
        cfg.priorities[0].signal_id = core::SIGNAL_CATALOG[3].id;
        cfg.priorities[1].signal_id = core::AutoBrightness::DIM_SIGNAL_ID;
        cfg.priorities[2].signal_id = 900;
        cfg.priorities[2].display_mode = core::DisplayMode::Gauge;
        store.save();
    }

    core::ConfigStore store(backing);
    store.load();
    const core::Config &cfg = store.get();
    TEST_ASSERT_EQUAL_UINT16(core::SIGNAL_CATALOG[3].id,
                             cfg.priorities[0].signal_id);
    TEST_ASSERT_EQUAL_UINT16(defaults[1], cfg.priorities[1].signal_id);
    TEST_ASSERT_EQUAL_UINT16(defaults[2], cfg.priorities[2].signal_id);
    TEST_ASSERT_TRUE(core::DisplayMode::Digital
                     == cfg.priorities[2].display_mode);

    /**
     * And saved that way.
     */
    core::Config stored {};
    backing.read(0, &stored, sizeof(stored));
    TEST_ASSERT_EQUAL_UINT16(defaults[1], stored.priorities[1].signal_id);
}

void test_signal_records_are_found_by_id()
{
    FakeConfigStore backing;

    {
        core::ConfigStore store(backing);
        store.load();
    }

    /**
     * Rearrange the stored table the way a different build would have
     * left it: Int Pres Act.'s record moved to slot 40, a retired signal
     * (id 900) sitting in slot 0, and Cool Temp missing entirely -- as if
     * it had just been promoted.
     */
    core::Config stored {};
    backing.read(0, &stored, sizeof(stored));

    int int_pres = core::signal_catalog_find(22);
    stored.signals[int_pres].warn  = { 1.0f, 2.0f, true, true };
    stored.signals[40]             = stored.signals[int_pres];
    stored.signals[int_pres]       = core::SignalConfig {};

    core::SignalConfig retired {};
    retired.signal_id = 900;
    retired.warn      = { 3.0f, 4.0f, false, true };
    stored.signals[0] = retired;
    backing.write(0, &stored, sizeof(stored));

    core::ConfigStore store(backing);
    store.load();
    const core::Config &cfg = store.get();

    for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
    {
        TEST_ASSERT_EQUAL_UINT16(core::SIGNAL_CATALOG[i].id,
                                 cfg.signals[i].signal_id);
    }
    TEST_ASSERT_EQUAL_FLOAT(2.0f, cfg.signals[int_pres].warn.hi);
    TEST_ASSERT_TRUE(cfg.signals[int_pres].warn.use_hi);
    TEST_ASSERT_FALSE(cfg.signals[0].warn.use_hi); // fresh for Cool Temp

    /**
     * The retired signal's settings are kept, past the catalog, for when
     * it comes back.
     */
    bool kept = false;
    for (size_t s = core::SIGNAL_CATALOG_COUNT; s < core::SIGNAL_CONFIG_SLOTS;
         s++)
    {
        if (cfg.signals[s].signal_id == 900)
        {
            kept = cfg.signals[s].warn.use_hi && cfg.signals[s].warn.hi == 4.0f;
        }
        else
        {
            TEST_ASSERT_EQUAL_UINT16(core::NO_SIGNAL_ID,
                                     cfg.signals[s].signal_id);
        }
    }
    TEST_ASSERT_TRUE(kept);

    /**
     * The sorted table was saved, so a partial write lands in the right
     * place from here on.
     */
    store.get().signals[int_pres].warn.lo = 9.0f;
    store.save_signal(22);
    core::ConfigStore store2(backing);
    store2.load();
    TEST_ASSERT_EQUAL_FLOAT(9.0f, store2.get().signals[int_pres].warn.lo);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, store2.get().signals[int_pres].warn.hi);
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
    RUN_TEST(test_newer_version_loads_defaults_without_overwriting);
    RUN_TEST(test_partial_save_over_newer_version_writes_whole_config);
    RUN_TEST(test_v6_config_survives_the_upgrade);
    RUN_TEST(test_v7_config_survives_the_upgrade);
    RUN_TEST(test_v8_config_survives_the_upgrade);
    RUN_TEST(test_v9_config_survives_the_upgrade);
    RUN_TEST(test_log_profile_round_trips);
    RUN_TEST(test_log_profile_drops_unoffered_and_repeated_ids);
    RUN_TEST(test_out_of_range_brightness_settings_load_as_defaults);
    RUN_TEST(test_panel_on_a_signal_not_offered_resets_to_default);
    RUN_TEST(test_signal_records_are_found_by_id);
    RUN_TEST(test_evaluate_warning);
    return UNITY_END();
}
