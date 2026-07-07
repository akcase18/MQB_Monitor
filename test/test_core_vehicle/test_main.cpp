/**
 * @file test_main.cpp
 * @brief Host-buildable round trip: UdsClient::send() -> SimulatorCanBus's
 * synthetic response -> UdsClient::poll() decode -> SignalStore combine.
 * Also covers core::Scheduler's due-task firing. Run with
 * `pio test -e simulator`.
 */
#include <unity.h>

#include "scheduler.hpp"
#include "signal_catalog.hpp"
#include "signal_store.hpp"
#include "simulator_can.hpp"
#include "uds_client.hpp"

namespace
{
    constexpr uint32_t TX_ID = 0x7E0;
    constexpr uint32_t RX_ID = 0x7E8;

    void on_response(void *ctx, uint8_t byte1, uint8_t byte2, float raw)
    {
        auto *store = static_cast<core::SignalStore *>(ctx);
        store->on_uds_response(byte1, byte2, raw, 0);
    }

    /* Deterministic IClock test double — real wall-clock time would make
     * Scheduler's due-task timing flaky under CI. */
    class FakeClock : public IClock
    {
        public:
            uint32_t millis() const override
            {
                return now_ms;
            }

            uint32_t now_ms = 0;
    };
} // namespace

void setUp()
{
}
void tearDown()
{
}

void test_single_request_round_trip()
{
    SimulatorCanBus &bus = SimulatorCanBus::instance();
    bus.begin(TX_ID, RX_ID, 500000);

    core::UdsClient   client(bus, TX_ID, RX_ID);
    core::SignalStore store;

    /* Signal 0: Coolant Temp, single request (0xF4, 0x05, offset -40). */
    const core::SignalSpec &spec = core::SIGNAL_CATALOG[0];
    client.send(spec.requests[0]);
    client.poll(on_response, &store);

    const core::SignalValue &value = store.get(spec.id);
    TEST_ASSERT_TRUE(value.has_data);

    /* SimulatorCanBus's fake_raw = (byte1*173 + byte2*37) % 4096. */
    uint16_t fake_raw = static_cast<uint16_t>(
        (spec.requests[0].byte1 * 173u + spec.requests[0].byte2 * 37u) % 4096u);
    float expected = (static_cast<float>(fake_raw) + spec.requests[0].offset)
                     * spec.requests[0].scale;
    TEST_ASSERT_EQUAL_FLOAT(expected, value.value);
}

void test_display_value_unit_conversion()
{
    SimulatorCanBus &bus = SimulatorCanBus::instance();
    bus.begin(TX_ID, RX_ID, 500000);

    core::UdsClient   client(bus, TX_ID, RX_ID);
    core::SignalStore store;

    const core::SignalSpec &spec = core::SIGNAL_CATALOG[0]; // Coolant Temp
    client.send(spec.requests[0]);
    client.poll(on_response, &store);

    float metric   = store.get_display_value(spec.id, true);
    float imperial = store.get_display_value(spec.id, false);

    /* Temp: imperial = metric * 1.8 + 32, then both truncated toward 0. */
    TEST_ASSERT_EQUAL_FLOAT(
        static_cast<float>(static_cast<int32_t>(metric * 1.8f + 32.0f)),
        imperial);
}

void test_multi_request_signal_waits_for_all_requests()
{
    SimulatorCanBus &bus = SimulatorCanBus::instance();
    bus.begin(TX_ID, RX_ID, 500000);

    core::UdsClient   client(bus, TX_ID, RX_ID);
    core::SignalStore store;

    /* Signal 6: Actual Intake Pressure = intake (req[1]) - ambient (req[0]). */
    const core::SignalSpec &spec = core::SIGNAL_CATALOG[6];
    TEST_ASSERT_EQUAL_UINT8(2, spec.num_requests);

    client.send(spec.requests[0]);
    client.poll(on_response, &store);
    TEST_ASSERT_FALSE(store.get(spec.id).has_data);

    client.send(spec.requests[1]);
    client.poll(on_response, &store);
    TEST_ASSERT_TRUE(store.get(spec.id).has_data);

    auto decode = [](const core::UdsRequestSpec &req) {
        uint16_t fake_raw = static_cast<uint16_t>(
            (req.byte1 * 173u + req.byte2 * 37u) % 4096u);
        return (static_cast<float>(fake_raw) + req.offset) * req.scale;
    };

    float expected = decode(spec.requests[1]) - decode(spec.requests[0]);
    TEST_ASSERT_EQUAL_FLOAT(expected, store.get(spec.id).value);
}

void test_signal_catalog_display_ranges_are_valid()
{
    for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
    {
        const core::SignalSpec &spec = core::SIGNAL_CATALOG[i];
        TEST_ASSERT_TRUE_MESSAGE(spec.display_min < spec.display_max,
                                 spec.name);
        TEST_ASSERT_TRUE_MESSAGE(core::warning_step(spec.unit_type) > 0.0f,
                                 spec.name);
    }
}

void test_unit_convert_round_trips()
{
    /* Regression for the warning-editor bug where a signal's imperial-unit
     * threshold was silently compared/edited against its metric value —
     * to_imperial(to_metric(x)) must return x for every convertible type. */
    const core::UnitType types[] = { core::UnitType::Pressure,
                                     core::UnitType::Speed,
                                     core::UnitType::Temp,
                                     core::UnitType::Torque,
                                     core::UnitType::Volume };
    for (core::UnitType type : types)
    {
        float metric     = 42.0f;
        float imperial   = core::unit_convert_to_imperial(type, metric);
        float round_trip = core::unit_convert_to_metric(type, imperial);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, metric, round_trip);
    }
}

void test_scheduler_fires_due_tasks()
{
    FakeClock       clock;
    core::Scheduler scheduler(clock);

    int          fire_count = 0;
    core::TaskId id         = scheduler.every(
        100, [](void *ctx) { (*static_cast<int *>(ctx))++; }, &fire_count);
    TEST_ASSERT_NOT_EQUAL(core::Scheduler::INVALID_TASK, id);

    clock.now_ms = 50;
    scheduler.tick();
    TEST_ASSERT_EQUAL_INT(0, fire_count);

    clock.now_ms = 100;
    scheduler.tick();
    TEST_ASSERT_EQUAL_INT(1, fire_count);

    clock.now_ms = 150;
    scheduler.tick();
    TEST_ASSERT_EQUAL_INT(1, fire_count);

    clock.now_ms = 200;
    scheduler.tick();
    TEST_ASSERT_EQUAL_INT(2, fire_count);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_single_request_round_trip);
    RUN_TEST(test_display_value_unit_conversion);
    RUN_TEST(test_multi_request_signal_waits_for_all_requests);
    RUN_TEST(test_signal_catalog_display_ranges_are_valid);
    RUN_TEST(test_unit_convert_round_trips);
    RUN_TEST(test_scheduler_fires_due_tasks);
    return UNITY_END();
}
