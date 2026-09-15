/**
 * @file test_main.cpp
 * @brief core::DeviceConfigStore -- the gauge product's second config
 * blob, holding sensor calibrations and the button map.
 *
 * The interesting cases here are all *rejection* cases. A display setting
 * read back wrong shows the wrong number of panels; a sensor calibration
 * read back wrong makes a gauge report a plausible, confident, incorrect
 * oil pressure, which is the single failure the whole conditioning layer
 * exists to prevent. So the header carries a shape fingerprint as well as
 * a magic, and a blob that disagrees about its shape is discarded whole
 * rather than partially trusted.
 *
 * Run with `pio test -e simulator`.
 */
#include <string.h>
#include <unity.h>

#include "config_store.hpp"
#include "device_config_store.hpp"

namespace
{
    class FakeConfigStore : public IConfigStore
    {
        public:
            static constexpr size_t CAPACITY = 4096;

            FakeConfigStore()
            {
                memset(bytes_, 0xFF, sizeof(bytes_));
            }

            bool read(uint32_t offset, void *buf, size_t len) override
            {
                if (offset + len > capacity_)
                {
                    return false;
                }
                memcpy(buf, bytes_ + offset, len);
                return true;
            }

            bool write(uint32_t offset, const void *buf, size_t len) override
            {
                if (offset + len > capacity_)
                {
                    return false;
                }
                memcpy(bytes_ + offset, buf, len);
                return true;
            }

            size_t capacity() const override
            {
                return capacity_;
            }

            /** Shrinks the reported capacity, for the too-small case. */
            void set_capacity(size_t bytes)
            {
                capacity_ = bytes;
            }

            /** Reaches into the raw bytes the way corrupt storage would. */
            uint8_t *raw()
            {
                return bytes_;
            }

        private:
            uint8_t bytes_[CAPACITY];
            size_t  capacity_ = CAPACITY;
    };

    /** A recognisable calibration, so a byte that survives a round trip is
     * distinguishable from a byte that happens to be zero. */
    core::SensorCal a_pressure_cal()
    {
        core::SensorCal cal {};
        memset(&cal, 0, sizeof(cal));

        cal.kind                  = core::SensorKind::LinearVoltage;
        cal.channel               = 2;
        cal.signal_id             = 7;
        cal.preset_id             = 1;
        cal.divider.r_top         = 10000.0f;
        cal.divider.r_bottom      = 15000.0f;
        cal.linear.v_lo           = 0.5f;
        cal.linear.value_lo       = 0.0f;
        cal.linear.v_hi           = 4.5f;
        cal.linear.value_hi       = 10.3421f;
        cal.linear.ratiometric    = true;
        cal.linear.nominal_supply = 5.0f;
        cal.filter_tau_s          = 0.25f;
        cal.band_margin           = 0.05f;
        return cal;
    }
} // namespace

void setUp()
{
}
void tearDown()
{
}

void test_a_blank_store_reports_a_fresh_device(void)
{
    FakeConfigStore          backing;
    core::DeviceConfigStore  store(backing);

    TEST_ASSERT_FALSE(store.load());

    /**
     * Not a rejection -- erased storage is the ordinary first boot, and
     * the root has to be able to tell that from losing a calibration
     * somebody entered. */
    TEST_ASSERT_FALSE(store.last_load_rejected());
    TEST_ASSERT_FALSE(store.storage_too_small());

    /**
     * And what it leaves behind is coherent, not half-read: every channel
     * says it is wired to nothing, which produces no reading at all
     * rather than a reading from an invented calibration. */
    for (uint8_t i = 0; i < core::SENSOR_MAX_CHANNELS; i++)
    {
        TEST_ASSERT_EQUAL(core::SensorKind::Unconfigured,
                          store.get().sensors[i].kind);
        TEST_ASSERT_EQUAL_UINT16(core::SENSOR_SIGNAL_NONE,
                                 store.get().sensors[i].signal_id);
    }
    TEST_ASSERT_EQUAL_UINT8(0, store.get().header.binding_count);
}

void test_a_calibration_survives_a_round_trip(void)
{
    FakeConfigStore backing;

    {
        core::DeviceConfigStore store(backing);
        store.load();

        core::DeviceConfig &dc    = store.get();
        dc.sensors[0]             = a_pressure_cal();
        dc.header.sensor_channels = 1;
        dc.warning_qualify_ms     = 1500;
        dc.sample_interval_ms     = 12;

        dc.bindings[0] = { 0,
                           core::ButtonEvent::ShortPress,
                           core::InputAction::CycleZoom };
        dc.bindings[1] = { 1,
                           core::ButtonEvent::DoublePress,
                           core::InputAction::ToggleUnits };
        dc.header.binding_count = 2;

        store.save();
    }

    /* A second store over the same bytes, as a power cycle would be. */
    core::DeviceConfigStore reloaded(backing);
    TEST_ASSERT_TRUE(reloaded.load());

    const core::DeviceConfig &dc = reloaded.get();
    TEST_ASSERT_EQUAL_UINT8(1, dc.header.sensor_channels);
    TEST_ASSERT_EQUAL(core::SensorKind::LinearVoltage, dc.sensors[0].kind);
    TEST_ASSERT_EQUAL_UINT8(2, dc.sensors[0].channel);
    TEST_ASSERT_EQUAL_UINT16(7, dc.sensors[0].signal_id);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 4.5f, dc.sensors[0].linear.v_hi);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 10.3421f, dc.sensors[0].linear.value_hi);
    TEST_ASSERT_TRUE(dc.sensors[0].linear.ratiometric);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, dc.sensors[0].filter_tau_s);

    TEST_ASSERT_EQUAL_UINT16(1500, dc.warning_qualify_ms);
    TEST_ASSERT_EQUAL_UINT32(12, dc.sample_interval_ms);

    TEST_ASSERT_EQUAL_UINT8(2, dc.header.binding_count);
    TEST_ASSERT_EQUAL(core::InputAction::CycleZoom, dc.bindings[0].action);
    TEST_ASSERT_EQUAL(core::InputAction::ToggleUnits, dc.bindings[1].action);
}

void test_a_partial_sensor_write_lands_on_the_right_channel(void)
{
    /**
     * What a web form's per-channel Save does: change four numbers without
     * rewriting eighteen hundred bytes. Getting the offset arithmetic
     * wrong here would overwrite a *neighbouring* sensor's curve, which is
     * both silent and exactly the kind of wrong reading this product
     * cannot afford. */
    FakeConfigStore backing;

    core::DeviceConfigStore store(backing);
    store.load();

    core::DeviceConfig &dc    = store.get();
    dc.header.sensor_channels = 4;
    for (uint8_t i = 0; i < 4; i++)
    {
        dc.sensors[i]             = a_pressure_cal();
        dc.sensors[i].channel     = i;
        dc.sensors[i].signal_id   = i;
        dc.sensors[i].linear.v_hi = 4.5f;
    }
    store.save();

    dc.sensors[2].linear.v_hi = 1.25f;
    store.save_sensor(2);

    core::DeviceConfigStore reloaded(backing);
    TEST_ASSERT_TRUE(reloaded.load());

    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f, 1.25f, reloaded.get().sensors[2].linear.v_hi);
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f, 4.5f, reloaded.get().sensors[1].linear.v_hi);
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f, 4.5f, reloaded.get().sensors[3].linear.v_hi);
}

void test_a_partial_binding_write_carries_its_count(void)
{
    /**
     * The count lives in the header and the table does not, so a partial
     * write that forgot the count would leave them disagreeing -- either
     * losing bindings or resurrecting stale ones. */
    FakeConfigStore backing;

    core::DeviceConfigStore store(backing);
    store.load();
    store.save();

    core::DeviceConfig &dc = store.get();
    dc.bindings[0]         = { 0,
                               core::ButtonEvent::LongPress,
                               core::InputAction::RampBrightness };
    dc.header.binding_count = 1;
    store.save_bindings();

    core::DeviceConfigStore reloaded(backing);
    TEST_ASSERT_TRUE(reloaded.load());
    TEST_ASSERT_EQUAL_UINT8(1, reloaded.get().header.binding_count);
    TEST_ASSERT_EQUAL(core::InputAction::RampBrightness,
                      reloaded.get().bindings[0].action);
}

void test_a_blob_that_disagrees_about_its_shape_is_rejected(void)
{
    /**
     * The whole reason the header carries more than a magic. Each of
     * these is a byte-for-byte plausible blob written by firmware that
     * thought something different about the struct. */
    struct Case
    {
            const char *what;
            size_t      field_offset;
            uint8_t     value;
    };

    const Case CASES[] = {
        { "version", offsetof(core::DeviceConfigHeader, version), 99 },
        { "catalog size",
          offsetof(core::DeviceConfigHeader, catalog_count),
          200 },
        { "struct size",
          offsetof(core::DeviceConfigHeader, struct_size),
          0x7F },
        { "channel count",
          offsetof(core::DeviceConfigHeader, sensor_channels),
          99 },
        { "binding count",
          offsetof(core::DeviceConfigHeader, binding_count),
          99 },
    };

    for (const Case &c : CASES)
    {
        FakeConfigStore backing;

        core::DeviceConfigStore store(backing);
        store.load();
        store.get().sensors[0]             = a_pressure_cal();
        store.get().header.sensor_channels = 1;
        store.save();

        backing.raw()[core::DEVICE_CONFIG_OFFSET + c.field_offset] = c.value;

        core::DeviceConfigStore reloaded(backing);
        TEST_ASSERT_FALSE_MESSAGE(reloaded.load(), c.what);

        /**
         * Rejected, not merely absent -- the caller needs to know a
         * configuration was thrown away rather than never written. */
        TEST_ASSERT_TRUE_MESSAGE(reloaded.last_load_rejected(), c.what);

        /**
         * And nothing partial survived. A half-trusted calibration is a
         * gauge reading a confident wrong number. */
        TEST_ASSERT_EQUAL_MESSAGE(core::SensorKind::Unconfigured,
                                  reloaded.get().sensors[0].kind,
                                  c.what);
    }
}

void test_a_bad_magic_reads_as_fresh_rather_than_rejected(void)
{
    FakeConfigStore backing;

    core::DeviceConfigStore store(backing);
    store.load();
    store.save();

    backing.raw()[core::DEVICE_CONFIG_OFFSET] ^= 0xFF;

    core::DeviceConfigStore reloaded(backing);
    TEST_ASSERT_FALSE(reloaded.load());
    /**
     * No magic means "nothing of ours is here", which is what erased
     * storage looks like too -- not the same thing as a blob of ours that
     * disagrees about its shape. */
    TEST_ASSERT_FALSE(reloaded.last_load_rejected());
}

void test_a_store_too_small_says_so_instead_of_looping_forever(void)
{
    /**
     * Without this the failure is invisible: every write fails, every
     * load finds nothing, and the device re-seeds factory defaults on
     * every boot while appearing to accept changes. */
    FakeConfigStore backing;
    backing.set_capacity(core::DEVICE_CONFIG_OFFSET + 16);

    core::DeviceConfigStore store(backing);
    TEST_ASSERT_FALSE(store.load());
    TEST_ASSERT_TRUE(store.storage_too_small());
}

void test_the_two_blobs_do_not_overlap(void)
{
    /**
     * The static_assert in the composition root says the same thing at
     * compile time, but only that one file sees both schemas. Asserted
     * again here so the suite fails rather than the product if the shared
     * schema ever grows past the offset. */
    TEST_ASSERT_TRUE(sizeof(core::Config) <= core::DEVICE_CONFIG_OFFSET);

    /**
     * And that the device blob's magic can never be mistaken for the
     * display blob's. */
    TEST_ASSERT_NOT_EQUAL(core::ConfigStore::MAGIC_NUMBER,
                          core::DEVICE_CONFIG_MAGIC);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_a_blank_store_reports_a_fresh_device);
    RUN_TEST(test_a_calibration_survives_a_round_trip);
    RUN_TEST(test_a_partial_sensor_write_lands_on_the_right_channel);
    RUN_TEST(test_a_partial_binding_write_carries_its_count);
    RUN_TEST(test_a_blob_that_disagrees_about_its_shape_is_rejected);
    RUN_TEST(test_a_bad_magic_reads_as_fresh_rather_than_rejected);
    RUN_TEST(test_a_store_too_small_says_so_instead_of_looping_forever);
    RUN_TEST(test_the_two_blobs_do_not_overlap);
    return UNITY_END();
}
