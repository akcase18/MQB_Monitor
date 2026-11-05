/**
 * @file test_main.cpp
 * @brief Host-buildable round trip: UdsClient::send() -> SimulatorCanBus's
 * synthetic response -> UdsClient::poll() decode -> SignalStore combine.
 * Also covers core::Scheduler's due-task firing and core::SignalPoller's
 * flow control -- the latter against a FakeEcu that models the one
 * property the real bus has and SimulatorCanBus does not: a response takes
 * time, so requests can outrun answers. Also internal signals (compiled,
 * polled and stored, never offered) and core::AutoBrightness. Run with
 * `pio test -e simulator`.
 */
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "auto_brightness.hpp"
#include "scheduler.hpp"
#include "signal_catalog.hpp"
#include "signal_poller.hpp"
#include "signal_store.hpp"
#include "simulator_can.hpp"
#include "uds_client.hpp"

namespace
{
    constexpr uint32_t TX_ID = 0x7E0;
    constexpr uint32_t RX_ID = 0x7E8;

    /**
     * The engine alone -- what every test against SIGNAL_CATALOG talks to.
     */
    const core::UdsEcu ENGINE[] = { { TX_ID, RX_ID } };

    /**
     * The engine plus the brake module, for the multi-module tests.
     */
    const core::UdsEcu TWO_MODULES[] = { { TX_ID, RX_ID }, { 0x713, 0x77D } };

    void on_response(void          *ctx,
                     uint8_t        ecu,
                     uint8_t        byte1,
                     uint8_t        byte2,
                     const uint8_t *payload,
                     uint8_t        len)
    {
        auto *store = static_cast<core::SignalStore *>(ctx);
        store->on_uds_response(ecu, byte1, byte2, payload, len, 0);
    }

    /**
     * Hands UdsClient::poll() whatever frames a test queues, once each, and
     * keeps what the client sends.
     */
    class QueuedBus : public ICanBus
    {
        public:
            bool begin(uint32_t, uint32_t, uint32_t) override
            {
                return true;
            }

            bool send(const CanFrame &frame) override
            {
                if (sent_count < 8)
                {
                    sent[sent_count] = frame;
                }
                sent_count++;
                return true;
            }

            bool receive(CanFrame &out) override
            {
                if (next_ >= count_)
                {
                    return false;
                }
                out = frames_[next_++];
                return true;
            }

            void push(const uint8_t *data, uint8_t len, uint32_t id = RX_ID)
            {
                CanFrame f {};
                f.id  = id;
                f.len = len;
                memcpy(f.data, data, len);
                frames_[count_++] = f;
            }

            CanFrame sent[8] {};
            uint8_t  sent_count = 0;

        private:
            CanFrame frames_[16];
            uint8_t  count_ = 0;
            uint8_t  next_  = 0;
    };

    struct Captured
    {
            uint8_t calls = 0;
            uint8_t ecu   = 0xFF;
            uint8_t byte1 = 0;
            uint8_t byte2 = 0;
            uint8_t len   = 0;
            uint8_t payload[core::UDS_MAX_PAYLOAD] {};
    };

    void capture_response(void          *ctx,
                          uint8_t        ecu,
                          uint8_t        byte1,
                          uint8_t        byte2,
                          const uint8_t *payload,
                          uint8_t        len)
    {
        auto *c  = static_cast<Captured *>(ctx);
        c->calls++;
        c->ecu   = ecu;
        c->byte1 = byte1;
        c->byte2 = byte2;
        c->len   = len;
        memcpy(c->payload, payload, len);
    }

    float decoded(const uint8_t *payload, uint8_t len, core::UdsField f)
    {
        float out = -12345.0f;
        TEST_ASSERT_TRUE(core::uds_decode_field(payload, len, f, out));
        return out;
    }

    core::UdsField field(uint8_t start,
                         uint8_t len,
                         bool    is_signed = false,
                         uint8_t shift     = 0,
                         uint8_t bits      = 0)
    {
        core::UdsField f;
        f.start     = start;
        f.len       = len;
        f.is_signed = is_signed;
        f.bit_shift = shift;
        f.bit_len   = bits;
        return f;
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

    /* An ECU that takes `latency_ms` to answer, holds exactly one request
     * at a time, and can be told to stay silent on chosen DIDs.
     *
     * SimulatorCanBus answers instantly and queues eight deep, which makes
     * every flow-control question untestable against it: a poller with no
     * flow control at all passes. The latency here is the whole point --
     * it is what lets demand exceed what the ECU can serve, which is the
     * condition under which one signal starves. */
    class FakeEcu : public ICanBus
    {
        public:
            static constexpr uint8_t MAX_TRACKED = 32;

            explicit FakeEcu(const FakeClock &clock)
                : clock_(clock)
            {
            }

            bool begin(uint32_t, uint32_t, uint32_t) override
            {
                return true;
            }

            bool send(const CanFrame &frame) override
            {
                uint8_t byte1 = frame.data[2];
                uint8_t byte2 = frame.data[3];
                slot(byte1, byte2).requests++;

                if (!silent(byte1, byte2))
                {
                    reply_b1_    = byte1;
                    reply_b2_    = byte2;
                    reply_at_ms_ = clock_.now_ms + latency_ms;
                    has_reply_   = true;
                }
                return true;
            }

            bool receive(CanFrame &out) override
            {
                if (!has_reply_
                    || static_cast<int32_t>(clock_.now_ms - reply_at_ms_) < 0)
                {
                    return false;
                }

                out         = CanFrame {};
                out.id      = RX_ID;
                out.len     = 8;
                out.data[1] = nrc_ != 0 ? 0x7F : 0x62;
                if (nrc_ != 0)
                {
                    out.data[0] = 0x03;
                    out.data[2] = 0x22;
                    out.data[3] = nrc_;
                }
                else
                {
                    out.data[0] = 0x05;
                    out.data[2] = reply_b1_;
                    out.data[3] = reply_b2_;
                    out.data[4] = 0x01;
                    out.data[5] = 0x00;
                }

                slot(reply_b1_, reply_b2_).responses++;
                has_reply_ = false;
                return true;
            }

            /** DIDs this ECU simply never answers. */
            void stay_silent_on(uint8_t byte1, uint8_t byte2)
            {
                silent_b1_  = byte1;
                silent_b2_  = byte2;
                has_silent_ = true;
            }

            /** Answer everything with this NRC instead of a reading. */
            void reply_with_nrc(uint8_t nrc)
            {
                nrc_ = nrc;
            }

            uint16_t requests_for(uint8_t byte1, uint8_t byte2)
            {
                return slot(byte1, byte2).requests;
            }

            uint16_t responses_for(uint8_t byte1, uint8_t byte2)
            {
                return slot(byte1, byte2).responses;
            }

            /** Every request this ECU has been asked, across all DIDs. */
            uint16_t total_requests() const
            {
                uint16_t total = 0;
                for (uint8_t i = 0; i < MAX_TRACKED; i++)
                {
                    total += tracked_[i].requests;
                }
                return total;
            }

            uint32_t latency_ms = 1;

        private:
            struct Tracked
            {
                    uint8_t  byte1     = 0;
                    uint8_t  byte2     = 0;
                    bool     used      = false;
                    uint16_t requests  = 0;
                    uint16_t responses = 0;
            };

            Tracked &slot(uint8_t byte1, uint8_t byte2)
            {
                for (uint8_t i = 0; i < MAX_TRACKED; i++)
                {
                    if (tracked_[i].used && tracked_[i].byte1 == byte1
                        && tracked_[i].byte2 == byte2)
                    {
                        return tracked_[i];
                    }
                }
                for (uint8_t i = 0; i < MAX_TRACKED; i++)
                {
                    if (!tracked_[i].used)
                    {
                        tracked_[i].used  = true;
                        tracked_[i].byte1 = byte1;
                        tracked_[i].byte2 = byte2;
                        return tracked_[i];
                    }
                }
                return tracked_[MAX_TRACKED - 1]; /* table full: lump it in */
            }

            bool silent(uint8_t byte1, uint8_t byte2) const
            {
                return has_silent_ && silent_b1_ == byte1
                       && silent_b2_ == byte2;
            }

            const FakeClock &clock_;
            Tracked          tracked_[MAX_TRACKED] {};

            bool     has_reply_   = false;
            uint8_t  reply_b1_    = 0;
            uint8_t  reply_b2_    = 0;
            uint32_t reply_at_ms_ = 0;

            bool    has_silent_ = false;
            uint8_t silent_b1_  = 0;
            uint8_t silent_b2_  = 0;

            uint8_t nrc_ = 0;
    };

    void on_poller_response(void          *ctx,
                            uint8_t        ecu,
                            uint8_t        byte1,
                            uint8_t        byte2,
                            const uint8_t *payload,
                            uint8_t        len)
    {
        (void)payload;
        (void)len;
        static_cast<core::SignalPoller *>(ctx)->on_response(ecu, byte1, byte2);
    }

    void on_poller_negative(void   *ctx,
                            uint8_t ecu,
                            uint8_t service,
                            uint8_t nrc)
    {
        static_cast<core::SignalPoller *>(ctx)->on_negative_response(
            ecu, service, nrc);
    }

    /** Runs `ms` milliseconds of main loop, one iteration per simulated
     * millisecond, in the same drain-then-send order loop() uses. */
    void run_loop(FakeClock          &clock,
                  core::UdsClient    &client,
                  core::SignalPoller &poller,
                  uint32_t            ms)
    {
        for (uint32_t i = 0; i < ms; i++)
        {
            clock.now_ms++;
            client.poll(
                on_poller_response, &poller, on_poller_negative, &poller);
            poller.tick();
        }
    }
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
    bus.begin(RX_ID, RX_ID, 500000);

    core::UdsClient   client(bus, ENGINE, 1);
    core::SignalStore store;

    /* Signal 0: Coolant Temp, single request (0xF4, 0x05, offset -40). */
    const core::SignalSpec &spec = core::SIGNAL_CATALOG[0];
    client.send(spec.requests[0]);
    client.poll(on_response, &store);

    const core::SignalValue &value = store.get(spec.id);
    TEST_ASSERT_TRUE(value.has_data);

    /* Asserted against the raw value the bus actually fabricated rather
     * than a copy of its formula here -- fabricated values carry a slow
     * drift (see simulator_can.cpp), so a duplicated formula would both
     * be wrong and silently rot. What's under test is that the decode
     * round-trips whatever was sent, not the specific number. */
    float expected
        = (static_cast<float>(bus.last_fake_raw()) + spec.requests[0].offset)
          * spec.requests[0].scale;
    TEST_ASSERT_EQUAL_FLOAT(expected, value.value);
}

void test_display_value_unit_conversion()
{
    SimulatorCanBus &bus = SimulatorCanBus::instance();
    bus.begin(RX_ID, RX_ID, 500000);

    core::UdsClient   client(bus, ENGINE, 1);
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
    bus.begin(RX_ID, RX_ID, 500000);

    core::UdsClient   client(bus, ENGINE, 1);
    core::SignalStore store;

    /* Catalog index 6: Int Pres Act. = intake (req[1]) - ambient (req[0]),
     * via the generic "difference" transform. */
    const core::SignalSpec &spec = core::SIGNAL_CATALOG[6];
    TEST_ASSERT_EQUAL_UINT8(2, spec.num_requests);

    auto decode = [](uint16_t raw, const core::UdsRequestSpec &req) {
        return (static_cast<float>(raw) + req.offset) * req.scale;
    };

    /* Each raw value is captured right after its own send(), since the
     * bus's drift means the two requests don't fabricate from the same
     * base -- see last_fake_raw()'s doc comment. */
    client.send(spec.requests[0]);
    float ambient = decode(bus.last_fake_raw(), spec.requests[0]);
    client.poll(on_response, &store);
    TEST_ASSERT_FALSE(store.get(spec.id).has_data);

    client.send(spec.requests[1]);
    float intake = decode(bus.last_fake_raw(), spec.requests[1]);
    client.poll(on_response, &store);
    TEST_ASSERT_TRUE(store.get(spec.id).has_data);

    float expected = intake - ambient;
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

void test_resolve_display_range_defaults_and_override()
{
    const core::SignalSpec &spec = core::SIGNAL_CATALOG[0]; // Coolant Temp

    core::DisplayRange no_override
        = core::resolve_display_range(spec, false, 0.0f, 50.0f);
    TEST_ASSERT_EQUAL_FLOAT(spec.display_min, no_override.min);
    TEST_ASSERT_EQUAL_FLOAT(spec.display_max, no_override.max);

    core::DisplayRange custom
        = core::resolve_display_range(spec, true, 0.0f, 50.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, custom.min);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, custom.max);

    /* Degenerate custom range (min >= max) falls back to the catalog
     * default rather than returning a zero/negative-width range. */
    core::DisplayRange degenerate
        = core::resolve_display_range(spec, true, 50.0f, 50.0f);
    TEST_ASSERT_EQUAL_FLOAT(spec.display_min, degenerate.min);
    TEST_ASSERT_EQUAL_FLOAT(spec.display_max, degenerate.max);
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
                                     core::UnitType::Volume,
                                     core::UnitType::Length,
                                     core::UnitType::TempDelta };
    for (core::UnitType type : types)
    {
        float metric     = 42.0f;
        float imperial   = core::unit_convert_to_imperial(type, metric);
        float round_trip = core::unit_convert_to_metric(type, imperial);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, metric, round_trip);
    }
}

/**
 * The same vectors as the monitor tool's check_auto_field_matches_legacy /
 * check_explicit_fields (tools/monitor/mqb_monitor/checks.py), so the two
 * decoders can't drift apart.
 */
void test_uds_decode_field_matches_the_monitor()
{
    const core::UdsField AUTO {};
    const uint8_t        one[]   = { 0xFF };
    const uint8_t        two[]   = { 0xFF, 0xFE };
    const uint8_t        three[] = { 0x12, 0x34, 0x56 };
    TEST_ASSERT_EQUAL_FLOAT(255.0f, decoded(one, 1, AUTO));
    TEST_ASSERT_EQUAL_FLOAT(-2.0f, decoded(two, 2, AUTO));
    TEST_ASSERT_EQUAL_FLOAT(4660.0f, decoded(three, 3, AUTO));

    const uint8_t p[] = { 0x03, 0x08, 0x24, 0x00, 0x04 }; /* cluster, 11 00 */
    TEST_ASSERT_EQUAL_FLOAT(36.0f, decoded(p, 5, field(2, 1)));
    TEST_ASSERT_EQUAL_FLOAT(776.0f, decoded(p, 5, field(0, 2)));
    TEST_ASSERT_EQUAL_FLOAT(136577028.0f, decoded(p, 5, field(1, 4)));

    const uint8_t s[] = { 0xF8, 0x30, 0x00 };
    TEST_ASSERT_EQUAL_FLOAT(-2000.0f, decoded(s, 3, field(0, 2, true)));
    TEST_ASSERT_EQUAL_FLOAT(63536.0f, decoded(s, 3, field(0, 2)));

    const uint8_t b[] = { 0xA5 };
    TEST_ASSERT_EQUAL_FLOAT(10.0f, decoded(b, 1, field(0, 1, false, 4, 4)));
    TEST_ASSERT_EQUAL_FLOAT(-6.0f, decoded(b, 1, field(0, 1, true, 4, 4)));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, decoded(b, 1, field(0, 1, false, 0, 1)));

    /**
     * Little-endian, low byte first: clutch torque, DID 38 2E.
     */
    core::UdsField le = field(0, 2);
    le.little_endian  = true;
    const uint8_t t[] = { 0x10, 0x00 };
    TEST_ASSERT_EQUAL_FLOAT(16.0f, decoded(t, 2, le));
    const uint8_t n[] = { 0xF8, 0xFF, 0x12 };
    le.is_signed      = true;
    TEST_ASSERT_EQUAL_FLOAT(-8.0f, decoded(n, 3, le));
    le                = field(1, 2);
    le.little_endian  = true;
    TEST_ASSERT_EQUAL_FLOAT(4863.0f, decoded(n, 3, le)); /* 0x12FF */

    float out = 0.0f;
    TEST_ASSERT_FALSE(core::uds_decode_field(p, 5, field(4, 2), out));
    TEST_ASSERT_FALSE(core::uds_decode_field(p, 0, AUTO, out));
}

void test_uds_client_passes_the_whole_payload()
{
    QueuedBus       bus;
    core::UdsClient client(bus, ENGINE, 1);
    Captured        got;

    /**
     * 9-byte-capable DID answered in one frame: 3 payload bytes.
     */
    const uint8_t three[] = { 0x06, 0x62, 0x11, 0x00, 0x03, 0x08, 0x24, 0xAA };
    /**
     * PCI length past the frame's real length: dropped, not decoded
     * against stale bytes.
     */
    const uint8_t lying[] = { 0x07, 0x62, 0x11, 0x00, 0x01 };
    bus.push(three, sizeof(three));
    bus.push(lying, sizeof(lying));
    client.poll(capture_response, &got);

    TEST_ASSERT_EQUAL_UINT8(1, got.calls);
    TEST_ASSERT_EQUAL_HEX8(0x11, got.byte1);
    TEST_ASSERT_EQUAL_HEX8(0x00, got.byte2);
    TEST_ASSERT_EQUAL_UINT8(3, got.len);
    TEST_ASSERT_EQUAL_HEX8(0x24, got.payload[2]);
}

void test_store_reads_each_request_field_and_skips_short_payloads()
{
    core::SignalStore       store;
    const core::SignalSpec &spec = core::SIGNAL_CATALOG[0]; // Cool Temp
    const core::UdsRequestSpec &req = spec.requests[0];

    /**
     * Auto field on a 3-byte payload: s16 from the first two bytes.
     */
    const uint8_t payload[] = { 0x00, 0x82, 0x55 };
    store.on_uds_response(req.ecu, req.byte1, req.byte2, payload, 3, 7);
    TEST_ASSERT_TRUE(store.get(spec.id).has_data);
    TEST_ASSERT_EQUAL_FLOAT((130.0f + req.offset) * req.scale,
                            store.get(spec.id).value);

    /**
     * An empty payload decodes to nothing: the old value stands.
     */
    store.on_uds_response(req.ecu, req.byte1, req.byte2, payload, 0, 9);
    TEST_ASSERT_EQUAL_UINT32(7, store.get(spec.id).last_update_ms);

    /**
     * The same DID from another module is a different value.
     */
    store.on_uds_response(
        static_cast<uint8_t>(req.ecu + 1), req.byte1, req.byte2, payload, 3, 11);
    TEST_ASSERT_EQUAL_UINT32(7, store.get(spec.id).last_update_ms);
}

void test_internal_signal_is_compiled_but_not_offered()
{
    constexpr uint16_t DIM = core::AutoBrightness::DIM_SIGNAL_ID;

    TEST_ASSERT_EQUAL_INT(-1, core::signal_catalog_find(DIM));

    int idx = core::signal_table_find(DIM);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(
        static_cast<int>(core::SIGNAL_CATALOG_COUNT), idx);
    TEST_ASSERT_LESS_THAN_INT(static_cast<int>(core::SIGNAL_TABLE_COUNT), idx);
    TEST_ASSERT_EQUAL_UINT16(DIM, core::SIGNAL_CATALOG[idx].id);

    /**
     * Every user-facing id is found by both lookups, at the same place.
     */
    for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
    {
        uint16_t id = core::SIGNAL_CATALOG[i].id;
        TEST_ASSERT_EQUAL_INT(static_cast<int>(i), core::signal_catalog_find(id));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(i), core::signal_table_find(id));
    }
}

void test_store_decodes_an_internal_signal()
{
    core::SignalStore           store;
    const core::SignalSpec     &spec = core::SIGNAL_CATALOG[
        core::signal_table_find(core::AutoBrightness::DIM_SIGNAL_ID)];
    const core::UdsRequestSpec &req = spec.requests[0];

    /**
     * A real answer from the car: byte 2 (0x24) is the needle curve.
     */
    const uint8_t payload[] = { 0x03, 0x08, 0x24, 0x00, 0x04 };
    store.on_uds_response(req.ecu, req.byte1, req.byte2, payload, 5, 12);

    const core::SignalValue &v = store.get(spec.id);
    TEST_ASSERT_TRUE(v.has_data);
    TEST_ASSERT_EQUAL_FLOAT(36.0f, v.value);
    TEST_ASSERT_EQUAL_UINT32(12, v.last_update_ms);
}

void test_poller_asks_for_an_internal_signal_only_when_listed()
{
    for (int listed = 0; listed < 2; listed++)
    {
        FakeClock          clock;
        FakeEcu            ecu(clock);
        core::UdsClient    client(ecu, core::UDS_ECUS, core::UDS_ECU_COUNT);
        core::SignalPoller poller(client, clock);

        const uint16_t ids[] = { 0, core::AutoBrightness::DIM_SIGNAL_ID };
        poller.set_active(ids, static_cast<uint8_t>(1 + listed));
        run_loop(clock, client, poller, 2000);

        uint16_t asked = ecu.requests_for(0x11, 0x00);
        if (listed)
        {
            TEST_ASSERT_GREATER_THAN_UINT16(0, asked);
        }
        else
        {
            TEST_ASSERT_EQUAL_UINT16(0, asked);
        }
    }
}

/** A fresh Dim Ctrl reading of `pct` taken at `at_ms`. */
core::SignalValue dim_reading(float pct, uint32_t at_ms)
{
    core::SignalValue v {};
    v.value          = pct;
    v.has_data       = true;
    v.last_update_ms = at_ms;
    return v;
}

void test_auto_brightness_level_for_clamps()
{
    TEST_ASSERT_EQUAL_UINT8(0, core::AutoBrightness::level_for(-5.0f));
    TEST_ASSERT_EQUAL_UINT8(0, core::AutoBrightness::level_for(0.0f));
    TEST_ASSERT_EQUAL_UINT8(36, core::AutoBrightness::level_for(36.0f));
    TEST_ASSERT_EQUAL_UINT8(37, core::AutoBrightness::level_for(36.6f));
    TEST_ASSERT_EQUAL_UINT8(100, core::AutoBrightness::level_for(100.0f));
    TEST_ASSERT_EQUAL_UINT8(100, core::AutoBrightness::level_for(255.0f));
}

void test_auto_brightness_slider_shifts_the_dimmer()
{
    using core::AutoBrightness;

    /**
     * 50 is 1:1, the ends +-25, linear between.
     */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, AutoBrightness::shift_for(50));
    TEST_ASSERT_EQUAL_FLOAT(25.0f, AutoBrightness::shift_for(100));
    TEST_ASSERT_EQUAL_FLOAT(-25.0f, AutoBrightness::shift_for(0));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, AutoBrightness::shift_for(70));
    TEST_ASSERT_EQUAL_FLOAT(25.0f, AutoBrightness::shift_for(200));

    TEST_ASSERT_EQUAL_UINT8(36, AutoBrightness::level_for(36.0f, 0.0f));
    TEST_ASSERT_EQUAL_UINT8(61, AutoBrightness::level_for(36.0f, 25.0f));
    TEST_ASSERT_EQUAL_UINT8(11, AutoBrightness::level_for(36.0f, -25.0f));

    /**
     * Capped both ways, and the byte's 101-255 counts as 100 before the
     * shift, not after.
     */
    TEST_ASSERT_EQUAL_UINT8(100, AutoBrightness::level_for(90.0f, 25.0f));
    TEST_ASSERT_EQUAL_UINT8(0, AutoBrightness::level_for(10.0f, -25.0f));
    TEST_ASSERT_EQUAL_UINT8(75, AutoBrightness::level_for(255.0f, -25.0f));

    /**
     * The shift moves the dimmer's level, not the fallback.
     */
    AutoBrightness ab;
    ab.start(50, 0);
    core::SignalValue none {};
    TEST_ASSERT_EQUAL_UINT8(80, ab.update(none, 25.0f, 80, 1000, true));
    core::SignalValue dim = dim_reading(40.0f, 1000);
    TEST_ASSERT_EQUAL_UINT8(65, ab.update(dim, 25.0f, 80, 1001, true));
}

void test_auto_brightness_ramps_toward_the_dimmer()
{
    core::AutoBrightness ab;
    ab.start(100, 0);

    /**
     * 100% a second: 250 ms in, a quarter of the way from 100 to 20.
     */
    core::SignalValue dim = dim_reading(20.0f, 0);
    TEST_ASSERT_EQUAL_UINT8(75, ab.update(dim, 0.0f, 80, 250));
    TEST_ASSERT_EQUAL_UINT8(50, ab.update(dim, 0.0f, 80, 500));

    /**
     * Arrives, and stops there rather than overshooting.
     */
    TEST_ASSERT_EQUAL_UINT8(20, ab.update(dim, 0.0f, 80, 1500));
    TEST_ASSERT_EQUAL_UINT8(20, ab.update(dim, 0.0f, 80, 1600));

    /**
     * Back up when the knob turns up.
     */
    dim = dim_reading(60.0f, 1600);
    TEST_ASSERT_EQUAL_UINT8(30, ab.update(dim, 0.0f, 80, 1700));
    TEST_ASSERT_EQUAL_UINT8(60, ab.update(dim, 0.0f, 80, 3000));
}

void test_auto_brightness_falls_back_without_the_cluster()
{
    core::AutoBrightness ab;
    ab.start(40, 0);

    /**
     * Never heard from: heads for the fallback.
     */
    core::SignalValue none {};
    TEST_ASSERT_EQUAL_UINT8(90, ab.update(none, 0.0f, 90, 1000));

    /**
     * A reading, then silence: held until STALE_MS, then the fallback.
     */
    core::SignalValue dim = dim_reading(10.0f, 1000);
    TEST_ASSERT_EQUAL_UINT8(10, ab.update(dim, 0.0f, 90, 2000));
    uint32_t stale_at = 1000 + core::AutoBrightness::STALE_MS;
    TEST_ASSERT_EQUAL_UINT8(10, ab.update(dim, 0.0f, 90, stale_at));
    TEST_ASSERT_EQUAL_UINT8(90, ab.update(dim, 0.0f, 90, stale_at + 1000));
}

void test_auto_brightness_start_moves_the_ramp_origin()
{
    core::AutoBrightness ab;
    ab.start(100, 0);
    core::SignalValue dim = dim_reading(0.0f, 0);
    ab.update(dim, 0.0f, 100, 2000); /* settled at 0 */

    /**
     * Re-entering Auto starts from what the panel shows, not from 0.
     */
    ab.start(70, 5000);
    dim = dim_reading(0.0f, 5000);
    TEST_ASSERT_EQUAL_UINT8(60, ab.update(dim, 0.0f, 100, 5100));
}

void test_auto_brightness_snaps_while_dark()
{
    core::AutoBrightness ab;
    ab.start(100, 0);

    /**
     * Panel dark: straight to the dimmer's level, however little time
     * passed. Lit again: ramps from there.
     */
    core::SignalValue dim = dim_reading(30.0f, 0);
    TEST_ASSERT_EQUAL_UINT8(30, ab.update(dim, 0.0f, 100, 10, true));
    dim = dim_reading(80.0f, 10);
    TEST_ASSERT_EQUAL_UINT8(40, ab.update(dim, 0.0f, 100, 110));
}

void test_enum_label_lookup()
{
    static const core::EnumEntry gears[] = { { 0, "1st" },
                                             { 1, "2nd" },
                                             { 8, "Shifting" },
                                             { -1, "Fault" } };
    core::SignalSpec spec {};
    spec.unit_type        = core::UnitType::Enum;
    spec.enum_entries     = gears;
    spec.num_enum_entries = 4;

    TEST_ASSERT_TRUE(core::signal_is_state(spec));
    TEST_ASSERT_EQUAL_STRING("1st", core::signal_enum_label(spec, 0.0f));
    TEST_ASSERT_EQUAL_STRING("Shifting", core::signal_enum_label(spec, 7.6f));
    TEST_ASSERT_EQUAL_STRING("Fault", core::signal_enum_label(spec, -1.2f));
    TEST_ASSERT_NULL(core::signal_enum_label(spec, 3.0f));
    TEST_ASSERT_EQUAL_STRING("", core::unit_label(core::UnitType::Enum, true));

    /**
     * Every compiled Enum fits a panel (gen_catalog.py enforces it).
     */
    for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
    {
        const core::SignalSpec &s = core::SIGNAL_CATALOG[i];
        TEST_ASSERT_EQUAL(s.unit_type == core::UnitType::Enum,
                          s.num_enum_entries > 0);
        for (uint8_t k = 0; k < s.num_enum_entries; k++)
        {
            TEST_ASSERT_TRUE(strlen(s.enum_entries[k].label)
                             <= core::ENUM_LABEL_MAX);
        }
    }
}

void test_new_units_convert_and_label()
{
    using core::UnitType;
    TEST_ASSERT_FLOAT_WITHIN(
        1e-5f, 1.0f, core::unit_convert_to_imperial(UnitType::Length, 25.4f));
    /**
     * A temperature DIFFERENCE scales but has no +32.
     */
    TEST_ASSERT_FLOAT_WITHIN(
        1e-5f, 18.0f, core::unit_convert_to_imperial(UnitType::TempDelta, 10.0f));
    TEST_ASSERT_EQUAL_STRING("in", core::unit_label(UnitType::Length, false));
    TEST_ASSERT_EQUAL_STRING("V", core::unit_label(UnitType::Voltage, true));
    TEST_ASSERT_EQUAL_STRING("lx", core::unit_label(UnitType::Illuminance, true));
    TEST_ASSERT_EQUAL_UINT8(2, core::unit_decimals(UnitType::Length, false));
    TEST_ASSERT_EQUAL_UINT8(2, core::unit_decimals(UnitType::Voltage, true));
    TEST_ASSERT_EQUAL_UINT8(1, core::unit_decimals(UnitType::Current, true));
    /**
     * Wire ordinals the phone page indexes by.
     */
    TEST_ASSERT_EQUAL(8, static_cast<int>(UnitType::Volume));
    TEST_ASSERT_EQUAL(15, static_cast<int>(UnitType::Enum));
}

void test_uds_client_routes_by_module()
{
    QueuedBus       bus;
    core::UdsClient client(bus, TWO_MODULES, 2);
    Captured        got;

    core::UdsRequestSpec req {};
    req.byte1 = 0x18;
    req.byte2 = 0x10;
    req.ecu   = 1;
    client.send(req);
    TEST_ASSERT_EQUAL_UINT8(1, bus.sent_count);
    TEST_ASSERT_EQUAL_HEX32(0x713, bus.sent[0].id);

    /**
     * An answer from a module outside the table is somebody else's.
     */
    const uint8_t reply[] = { 0x05, 0x62, 0x18, 0x10, 0x0C, 0x9E, 0xAA, 0xAA };
    bus.push(reply, sizeof(reply), 0x7E9);
    bus.push(reply, sizeof(reply), 0x77D);
    client.poll(capture_response, &got);
    TEST_ASSERT_EQUAL_UINT8(1, got.calls);
    TEST_ASSERT_EQUAL_UINT8(1, got.ecu);
    TEST_ASSERT_EQUAL_UINT8(2, got.len);
}

void test_uds_client_reassembles_a_multi_frame_answer()
{
    QueuedBus       bus;
    core::UdsClient client(bus, TWO_MODULES, 2);
    Captured        got;

    /**
     * Brake booster 18 10: 9 data bytes, so 12 with the header -- a first
     * frame carrying 6 and one consecutive frame carrying the rest.
     */
    const uint8_t first[] = { 0x10, 0x0C, 0x62, 0x18, 0x10, 0x01, 0x0C, 0x9E };
    const uint8_t next[]  = { 0x21, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0xAA };
    bus.push(first, sizeof(first), 0x77D);
    client.poll(capture_response, &got);

    TEST_ASSERT_TRUE(client.receiving());
    TEST_ASSERT_EQUAL_UINT8(0, got.calls);
    TEST_ASSERT_EQUAL_UINT8(1, bus.sent_count);
    TEST_ASSERT_EQUAL_HEX32(0x713, bus.sent[0].id); /* flow control to it */
    TEST_ASSERT_EQUAL_HEX8(0x30, bus.sent[0].data[0]);
    TEST_ASSERT_EQUAL_UINT8(3, bus.sent[0].data[1]);

    bus.push(next, sizeof(next), 0x77D);
    client.poll(capture_response, &got);
    TEST_ASSERT_FALSE(client.receiving());
    TEST_ASSERT_EQUAL_UINT8(1, got.calls);
    TEST_ASSERT_EQUAL_UINT8(1, got.ecu);
    TEST_ASSERT_EQUAL_HEX8(0x18, got.byte1);
    TEST_ASSERT_EQUAL_UINT8(9, got.len);
    const uint8_t want[] = { 0x01, 0x0C, 0x9E, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got.payload, 9);
}

void test_uds_client_flow_controls_in_blocks_and_refuses_overlong()
{
    QueuedBus       bus;
    core::UdsClient client(bus, ENGINE, 1);
    Captured        got;

    /**
     * The longest answer it takes: 3 header + 32 data = 35 bytes, a first
     * frame and five consecutive frames, with a fresh flow control after
     * the third.
     */
    uint8_t first[] = { 0x10, 35, 0x62, 0x22, 0x01, 0, 1, 2 };
    bus.push(first, sizeof(first));
    uint8_t value = 3;
    for (uint8_t seq = 1; seq <= 5; seq++)
    {
        uint8_t cf[8] = { static_cast<uint8_t>(0x20 | seq) };
        for (uint8_t k = 1; k < 8; k++)
        {
            cf[k] = value++;
        }
        bus.push(cf, sizeof(cf));
    }
    client.poll(capture_response, &got);

    TEST_ASSERT_EQUAL_UINT8(2, bus.sent_count);
    TEST_ASSERT_EQUAL_HEX8(0x30, bus.sent[1].data[0]);
    TEST_ASSERT_EQUAL_UINT8(1, got.calls);
    TEST_ASSERT_EQUAL_UINT8(core::UDS_MAX_PAYLOAD, got.len);
    for (uint8_t k = 0; k < core::UDS_MAX_PAYLOAD; k++)
    {
        TEST_ASSERT_EQUAL_UINT8(k, got.payload[k]);
    }

    /**
     * One byte longer: refused with an overflow, nothing waited for.
     */
    QueuedBus       bus2;
    core::UdsClient client2(bus2, ENGINE, 1);
    uint8_t         too_long[] = { 0x10, 36, 0x62, 0x22, 0x01, 0, 1, 2 };
    bus2.push(too_long, sizeof(too_long));
    client2.poll(capture_response, &got);
    TEST_ASSERT_FALSE(client2.receiving());
    TEST_ASSERT_EQUAL_HEX8(0x32, bus2.sent[0].data[0]);
}

void test_uds_client_drops_an_answer_with_a_missing_frame()
{
    QueuedBus       bus;
    core::UdsClient client(bus, ENGINE, 1);
    Captured        got;

    const uint8_t first[] = { 0x10, 0x14, 0x62, 0x11, 0x00, 1, 2, 3 };
    const uint8_t skip[]  = { 0x22, 4, 5, 6, 7, 8, 9, 10 }; /* #1 missed */
    bus.push(first, sizeof(first));
    bus.push(skip, sizeof(skip));
    client.poll(capture_response, &got);

    TEST_ASSERT_FALSE(client.receiving());
    TEST_ASSERT_EQUAL_UINT8(0, got.calls);
}

void test_poller_waits_through_a_multi_frame_answer_but_not_forever()
{
    FakeClock          clock;
    QueuedBus          bus;
    core::UdsClient    client(bus, ENGINE, 1);
    core::SignalPoller poller(client, clock);

    /**
     * The fastest-polled single-request signal, so it's due again well
     * inside the grace period and only the wait itself holds it back.
     */
    size_t fastest = 0;
    for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
    {
        const core::SignalSpec &spec = core::SIGNAL_CATALOG[i];
        if (spec.num_requests == 1
            && spec.cycle_time_ms < core::SIGNAL_CATALOG[fastest].cycle_time_ms)
        {
            fastest = i;
        }
    }
    const core::SignalSpec &spec = core::SIGNAL_CATALOG[fastest];
    TEST_ASSERT_TRUE(spec.cycle_time_ms < core::SignalPoller::RESPONSE_TIMEOUT_MS);

    poller.set_active(&spec.id, 1);
    poller.tick();
    TEST_ASSERT_EQUAL_UINT8(1, bus.sent_count);

    /**
     * A first frame arrives and the rest is slow: past the single-frame
     * deadline the poller keeps waiting rather than asking again...
     */
    const core::UdsRequestSpec &req = spec.requests[0];
    const uint8_t first[] = { 0x10, 0x0A, 0x62, req.byte1, req.byte2, 1, 2, 3 };
    bus.push(first, sizeof(first));
    run_loop(clock, client, poller, core::SignalPoller::RESPONSE_TIMEOUT_MS + 50);
    TEST_ASSERT_EQUAL_UINT8(2, bus.sent_count); /* the request + one FC */

    /**
     * ...but a module that stops part-way doesn't hold the bus for good.
     */
    run_loop(clock, client, poller, core::SignalPoller::MULTI_FRAME_GRACE_MS);
    TEST_ASSERT_EQUAL_UINT8(3, bus.sent_count);
    TEST_ASSERT_EQUAL_HEX8(0x22, bus.sent[2].data[1]);
}

void test_signal_catalog_names_are_unique()
{
    /* A panel and the signal pickers show the bare name, so two signals
     * sharing one couldn't be told apart (tools/gen_catalog.py refuses it). */
    for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
    {
        for (size_t k = i + 1; k < core::SIGNAL_CATALOG_COUNT; k++)
        {
            TEST_ASSERT_TRUE_MESSAGE(
                strcmp(core::SIGNAL_CATALOG[i].name,
                       core::SIGNAL_CATALOG[k].name)
                    != 0,
                core::SIGNAL_CATALOG[i].name);
        }
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

/* Catalog DIDs the poller tests reason about, named so an assertion below
 * reads as the signal it is really about. */
namespace did
{
    constexpr uint8_t INT_PRES[2]      = { 0x3E, 0x70 }; /* id 4, 50ms */
    constexpr uint8_t AMB_PRES[2]      = { 0x13, 0xCA }; /* id 5, 1000ms */
    constexpr uint8_t ENGINE_SPEED[2]  = { 0x20, 0x6E }; /* id 7, 50ms */
    constexpr uint8_t ENGINE_TORQUE[2] = { 0x43, 0x7C }; /* id 8, 50ms */
    constexpr uint8_t THROTTLE[2]      = { 0xF4, 0x11 }; /* id 12, 50ms */
    constexpr uint8_t COOL_TEMP[2]     = { 0xF4, 0x05 }; /* id 0, 2000ms */
} // namespace did

void test_poller_only_requests_displayed_signals()
{
    FakeClock          clock;
    FakeEcu            ecu(clock);
    core::UdsClient    client(ecu, ENGINE, 1);
    core::SignalPoller poller(client, clock);

    /* Engine Speed and Engine Torque on panels; Throttle Pos and Coolant
     * Temp in the catalog but on no panel. */
    const uint16_t active[] = { 7, 8 };
    poller.set_active(active, 2);

    run_loop(clock, client, poller, 1000);

    TEST_ASSERT_GREATER_THAN_UINT16(
        0, ecu.requests_for(did::ENGINE_SPEED[0], did::ENGINE_SPEED[1]));
    TEST_ASSERT_GREATER_THAN_UINT16(
        0, ecu.requests_for(did::ENGINE_TORQUE[0], did::ENGINE_TORQUE[1]));

    /* The old one-scheduler-task-per-catalog-entry model asked for these
     * every cycle whether or not anything could show them. */
    TEST_ASSERT_EQUAL_UINT16(
        0, ecu.requests_for(did::THROTTLE[0], did::THROTTLE[1]));
    TEST_ASSERT_EQUAL_UINT16(
        0, ecu.requests_for(did::COOL_TEMP[0], did::COOL_TEMP[1]));
}

/** True if `id` is one of `count` displayed signal ids. */
bool is_displayed(uint16_t id, const uint16_t *displayed, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++)
    {
        if (displayed[i] == id)
        {
            return true;
        }
    }
    return false;
}

/** True if any displayed signal owns this exact DID -- one response
 * answers every catalog entry that reads it, so a DID reaching the wire
 * on a displayed signal's behalf is not a leak. */
bool displayed_signal_owns(const core::UdsRequestSpec &req,
                           const uint16_t             *displayed,
                           uint8_t                     count)
{
    for (uint8_t i = 0; i < count; i++)
    {
        int idx = core::signal_catalog_find(displayed[i]);
        if (idx < 0)
        {
            continue;
        }

        const core::SignalSpec &shown = core::SIGNAL_CATALOG[idx];
        for (uint8_t r = 0; r < shown.num_requests; r++)
        {
            if (shown.requests[r].byte1 == req.byte1
                && shown.requests[r].byte2 == req.byte2)
            {
                return true;
            }
        }
    }
    return false;
}

void test_poller_asks_for_nothing_outside_the_displayed_panels()
{
    FakeClock          clock;
    FakeEcu            ecu(clock);
    core::UdsClient    client(ecu, ENGINE, 1);
    core::SignalPoller poller(client, clock);

    /* Three panels, the way a user might actually set the dashboard up:
     * Coolant Temp (2000ms), Engine Speed (50ms), Throttle Pos (50ms). */
    const uint16_t displayed[]   = { 0, 7, 12 };
    const uint8_t  displayed_len = 3;
    poller.set_active(displayed, displayed_len);

    run_loop(clock, client, poller, 2000);

    /* The whole catalog, not a hand-picked pair: every request of every
     * signal that is not on a panel must never have reached the wire.
     * This is the assertion that would have failed outright under the
     * old model, which registered a poll task per catalog entry. */
    for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
    {
        const core::SignalSpec &spec = core::SIGNAL_CATALOG[i];
        if (is_displayed(spec.id, displayed, displayed_len))
        {
            continue;
        }

        for (uint8_t r = 0; r < spec.num_requests; r++)
        {
            const core::UdsRequestSpec &req = spec.requests[r];
            if (displayed_signal_owns(req, displayed, displayed_len))
            {
                continue;
            }

            TEST_ASSERT_EQUAL_UINT16_MESSAGE(
                0, ecu.requests_for(req.byte1, req.byte2), spec.name);
        }
    }

    /* All three displayed signals are genuinely being read, so the above
     * cannot pass by the poller simply doing nothing. */
    TEST_ASSERT_GREATER_THAN_UINT16(
        0, ecu.responses_for(did::COOL_TEMP[0], did::COOL_TEMP[1]));
    TEST_ASSERT_GREATER_THAN_UINT16(
        0, ecu.responses_for(did::ENGINE_SPEED[0], did::ENGINE_SPEED[1]));
    TEST_ASSERT_GREATER_THAN_UINT16(
        0, ecu.responses_for(did::THROTTLE[0], did::THROTTLE[1]));

    /* And the point of all this: total bus and loop work is bounded by
     * what is on screen. Two 50ms signals plus one 2000ms signal over 2s
     * is ~81 requests. The 22-signal catalog polled at its own cadences
     * is ~400 over the same window. */
    TEST_ASSERT_LESS_THAN_UINT16(100, ecu.total_requests());
}

void test_poller_keeps_one_request_outstanding()
{
    FakeClock          clock;
    FakeEcu            ecu(clock);
    core::UdsClient    client(ecu, ENGINE, 1);
    core::SignalPoller poller(client, clock);

    /* Four 50ms signals, all due at once — the burst that used to put six
     * frames on the wire inside ~1.3ms. */
    const uint16_t active[] = { 4, 7, 8, 12 };
    poller.set_active(active, 4);

    /* An ECU that never answers, so nothing can legitimately advance. */
    ecu.latency_ms = 1000000;

    for (uint32_t i = 0; i < core::SignalPoller::RESPONSE_TIMEOUT_MS - 1; i++)
    {
        clock.now_ms++;
        poller.tick();
    }

    uint16_t total
        = ecu.requests_for(did::INT_PRES[0], did::INT_PRES[1])
          + ecu.requests_for(did::ENGINE_SPEED[0], did::ENGINE_SPEED[1])
          + ecu.requests_for(did::ENGINE_TORQUE[0], did::ENGINE_TORQUE[1])
          + ecu.requests_for(did::THROTTLE[0], did::THROTTLE[1]);
    TEST_ASSERT_EQUAL_UINT16(1, total);
}

void test_poller_deduplicates_a_shared_did()
{
    FakeClock          clock;
    FakeEcu            ecu(clock);
    core::UdsClient    client(ecu, ENGINE, 1);
    core::SignalPoller poller(client, clock);

    /* Int Pres (0x3E70) and Int Pres Act. (0x13CA + 0x3E70) share a DID,
     * and SignalStore fans one response out to both — so 0x3E70 must go
     * out once per cycle, not once per signal that wants it. */
    const uint16_t active[] = { 4, 22 };
    poller.set_active(active, 2);

    run_loop(clock, client, poller, 1000);

    uint16_t shared  = ecu.requests_for(did::INT_PRES[0], did::INT_PRES[1]);
    uint16_t only_22 = ecu.requests_for(did::AMB_PRES[0], did::AMB_PRES[1]);

    TEST_ASSERT_GREATER_THAN_UINT16(0, only_22);
    /* Asked at the same 50ms cadence as the DID only one signal wants,
     * rather than at twice it. */
    TEST_ASSERT_INT_WITHIN(2, only_22, shared);
}

void test_poller_serves_every_signal_when_the_ecu_cannot_keep_up()
{
    FakeClock          clock;
    FakeEcu            ecu(clock);
    core::UdsClient    client(ecu, ENGINE, 1);
    core::SignalPoller poller(client, clock);

    /* The reported hardware failure, reproduced: five panels, four of them
     * on the same 50ms cadence, against an ECU slow enough that demand
     * (~80 req/s for the 50ms group) exceeds capacity (~66 req/s). Under
     * the old model the tail of each burst was dropped and one signal --
     * Throttle Pos, last in catalog order among the 50ms group -- updated
     * almost never while the rest looked fine. */
    ecu.latency_ms = 15;

    const uint16_t active[] = { 4, 7, 8, 12, 0 };
    poller.set_active(active, 5);

    run_loop(clock, client, poller, 3000);

    uint16_t counts[] = {
        ecu.responses_for(did::INT_PRES[0], did::INT_PRES[1]),
        ecu.responses_for(did::ENGINE_SPEED[0], did::ENGINE_SPEED[1]),
        ecu.responses_for(did::ENGINE_TORQUE[0], did::ENGINE_TORQUE[1]),
        ecu.responses_for(did::THROTTLE[0], did::THROTTLE[1]),
    };

    uint16_t lowest  = counts[0];
    uint16_t highest = counts[0];
    for (uint8_t i = 1; i < 4; i++)
    {
        if (counts[i] < lowest)
        {
            lowest = counts[i];
        }
        if (counts[i] > highest)
        {
            highest = counts[i];
        }
    }

    /* Real throughput, not a trickle: 3s of a ~15ms round trip shared four
     * ways is ~50 readings each. */
    TEST_ASSERT_GREATER_THAN_UINT16(30, lowest);

    /* Nobody is systematically behind: serving an entry pushes it a full
     * cycle into the future, so equal-cadence signals take turns instead
     * of the front of the burst crowding out the back. */
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(3, highest - lowest);
}

void test_poller_does_not_starve_a_slow_signal_behind_fast_ones()
{
    FakeClock          clock;
    FakeEcu            ecu(clock);
    core::UdsClient    client(ecu, ENGINE, 1);
    core::SignalPoller poller(client, clock);

    /* Two 50ms signals and one 2000ms signal, against an ECU slower than
     * the fast pair's cadence — so something is always overdue and the
     * poller never gets an idle moment to spend on the slow one.
     *
     * This is what pick_due()'s most-overdue-first choice is for, and the
     * case equal cadences cannot show: picking the first due entry in
     * table order instead would hand every slot to one of the two 50ms
     * DIDs, which are always due and always earlier in the table, and
     * Coolant Temp would never be read once. Overdue-ness accumulating on
     * the skipped entry is the only thing that lets it through. */
    ecu.latency_ms = 40;

    const uint16_t active[] = { 7, 8, 0 };
    poller.set_active(active, 3);

    run_loop(clock, client, poller, 3000);

    TEST_ASSERT_GREATER_THAN_UINT16(
        0, ecu.responses_for(did::COOL_TEMP[0], did::COOL_TEMP[1]));

    /* And not by starving the fast pair to feed it. */
    TEST_ASSERT_GREATER_THAN_UINT16(
        20, ecu.responses_for(did::ENGINE_SPEED[0], did::ENGINE_SPEED[1]));
    TEST_ASSERT_GREATER_THAN_UINT16(
        20, ecu.responses_for(did::ENGINE_TORQUE[0], did::ENGINE_TORQUE[1]));
}

void test_poller_recovers_from_an_unanswered_did()
{
    FakeClock          clock;
    FakeEcu            ecu(clock);
    core::UdsClient    client(ecu, ENGINE, 1);
    core::SignalPoller poller(client, clock);

    /* One DID this ECU does not support, sharing the schedule with two it
     * does. Without backoff it would burn a full RESPONSE_TIMEOUT_MS every
     * 50ms cycle, which is the same starvation in a different costume. */
    ecu.stay_silent_on(did::THROTTLE[0], did::THROTTLE[1]);

    const uint16_t active[] = { 7, 8, 12 };
    poller.set_active(active, 3);

    run_loop(clock, client, poller, 3000);

    uint16_t dead = ecu.requests_for(did::THROTTLE[0], did::THROTTLE[1]);
    uint16_t alive
        = ecu.responses_for(did::ENGINE_SPEED[0], did::ENGINE_SPEED[1]);

    /* Backed off to BACKOFF_PERIOD_MS: a handful of attempts over 3s, not
     * one every 50ms. */
    TEST_ASSERT_LESS_THAN_UINT16(3000 / core::SignalPoller::BACKOFF_PERIOD_MS
                                     + core::SignalPoller::MISSES_BEFORE_BACKOFF
                                     + 2,
                                 dead);

    /* And the signals that do answer keep their cadence. */
    TEST_ASSERT_GREATER_THAN_UINT16(50, alive);
}

void test_poller_treats_a_rejection_as_the_end_of_a_request()
{
    FakeClock          clock;
    FakeEcu            ecu(clock);
    core::UdsClient    client(ecu, ENGINE, 1);
    core::SignalPoller poller(client, clock);

    /* requestOutOfRange, arriving well inside the response timeout. */
    ecu.reply_with_nrc(0x31);
    ecu.latency_ms = 2;

    const uint16_t active[] = { 7, 8 };
    poller.set_active(active, 2);

    run_loop(
        clock, client, poller, core::SignalPoller::RESPONSE_TIMEOUT_MS - 1);

    /* Two requests inside one timeout window is only possible if the NRC
     * freed the in-flight slot instead of the deadline expiring. */
    uint16_t total
        = ecu.requests_for(did::ENGINE_SPEED[0], did::ENGINE_SPEED[1])
          + ecu.requests_for(did::ENGINE_TORQUE[0], did::ENGINE_TORQUE[1]);
    TEST_ASSERT_GREATER_THAN_UINT16(1, total);
}

void test_poller_waits_through_a_response_pending()
{
    FakeClock          clock;
    FakeEcu            ecu(clock);
    core::UdsClient    client(ecu, ENGINE, 1);
    core::SignalPoller poller(client, clock);

    /* 0x78 is the one NRC that does not end a request: the ECU is saying
     * "still working". Sending the next request on top of it is exactly
     * the overrun this class exists to prevent. */
    ecu.reply_with_nrc(0x78);
    ecu.latency_ms = 2;

    const uint16_t active[] = { 7, 8 };
    poller.set_active(active, 2);

    run_loop(
        clock, client, poller, core::SignalPoller::RESPONSE_TIMEOUT_MS - 1);

    uint16_t total
        = ecu.requests_for(did::ENGINE_SPEED[0], did::ENGINE_SPEED[1])
          + ecu.requests_for(did::ENGINE_TORQUE[0], did::ENGINE_TORQUE[1]);
    TEST_ASSERT_EQUAL_UINT16(1, total);
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
    RUN_TEST(test_resolve_display_range_defaults_and_override);
    RUN_TEST(test_unit_convert_round_trips);
    RUN_TEST(test_uds_decode_field_matches_the_monitor);
    RUN_TEST(test_uds_client_passes_the_whole_payload);
    RUN_TEST(test_store_reads_each_request_field_and_skips_short_payloads);
    RUN_TEST(test_internal_signal_is_compiled_but_not_offered);
    RUN_TEST(test_store_decodes_an_internal_signal);
    RUN_TEST(test_poller_asks_for_an_internal_signal_only_when_listed);
    RUN_TEST(test_auto_brightness_level_for_clamps);
    RUN_TEST(test_auto_brightness_slider_shifts_the_dimmer);
    RUN_TEST(test_auto_brightness_ramps_toward_the_dimmer);
    RUN_TEST(test_auto_brightness_falls_back_without_the_cluster);
    RUN_TEST(test_auto_brightness_start_moves_the_ramp_origin);
    RUN_TEST(test_auto_brightness_snaps_while_dark);
    RUN_TEST(test_enum_label_lookup);
    RUN_TEST(test_new_units_convert_and_label);
    RUN_TEST(test_uds_client_routes_by_module);
    RUN_TEST(test_uds_client_reassembles_a_multi_frame_answer);
    RUN_TEST(test_uds_client_flow_controls_in_blocks_and_refuses_overlong);
    RUN_TEST(test_uds_client_drops_an_answer_with_a_missing_frame);
    RUN_TEST(test_poller_waits_through_a_multi_frame_answer_but_not_forever);
    RUN_TEST(test_signal_catalog_names_are_unique);
    RUN_TEST(test_scheduler_fires_due_tasks);
    RUN_TEST(test_poller_only_requests_displayed_signals);
    RUN_TEST(test_poller_asks_for_nothing_outside_the_displayed_panels);
    RUN_TEST(test_poller_keeps_one_request_outstanding);
    RUN_TEST(test_poller_deduplicates_a_shared_did);
    RUN_TEST(test_poller_serves_every_signal_when_the_ecu_cannot_keep_up);
    RUN_TEST(test_poller_does_not_starve_a_slow_signal_behind_fast_ones);
    RUN_TEST(test_poller_recovers_from_an_unanswered_did);
    RUN_TEST(test_poller_treats_a_rejection_as_the_end_of_a_request);
    RUN_TEST(test_poller_waits_through_a_response_pending);
    return UNITY_END();
}
