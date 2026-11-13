/**
 * @file test_main.cpp
 * @brief Host-buildable coverage for core/bench's BenchEngine, the v4
 * dash's bench mode: the responder's answers (lengths, latency, multi-DID
 * and multi-frame both ways, 0x78, dropped requests, suppressed tester
 * present), the tester's closed loop against a responder over a simulated
 * wire (single- and multi-frame requests and answers, timeouts,
 * concurrent lanes), and the frame log. Run with
 * `pio test -e simulator -f test_core_bench`.
 */
#include <string.h>
#include <unity.h>

#include <vector>

#include "bench_engine.hpp"

namespace
{
    using core::bench::BenchEngine;
    using core::bench::BenchFrame;
    using core::bench::IBenchBus;
    using core::bench::Role;

    /**
     * Shared microsecond clock for every engine in a test.
     */
    uint32_t g_now_us = 0;

    /**
     * Roughly an 8-byte classic frame at 500 kbit/s.
     */
    constexpr uint32_t FRAME_US = 250;

    struct InFlight
    {
            uint32_t arrive_us;
            int      to;
            CanFrame frame;
    };

    /**
     * Two engines on one simulated wire: a frame sent by either arrives at
     * the other FRAME_US later. A lone engine (peer -1) just records what
     * it sends.
     */
    struct Wire
    {
            std::vector<InFlight> in_flight;
            std::vector<CanFrame> sent[2];
    };

    class FakeBus : public IBenchBus
    {
        public:
            FakeBus(Wire &wire, int self, int peer)
                : wire_(wire)
                , self_(self)
                , peer_(peer)
            {
            }

            bool send(const CanFrame &frame) override
            {
                wire_.sent[self_].push_back(frame);
                if (peer_ >= 0)
                {
                    wire_.in_flight.push_back(
                        InFlight { g_now_us + FRAME_US, peer_, frame });
                }
                return true;
            }

            uint32_t micros() const override
            {
                return g_now_us;
            }

        private:
            Wire &wire_;
            int   self_;
            int   peer_;
    };

    constexpr uint32_t LOG_SIZE = 4096;

    BenchFrame g_log[2][LOG_SIZE];

    /**
     * A responder (engine 0) and a tester (engine 1) on one wire, with
     * the engine module at 7E0/7E8 and the transmission at 7E1/7E9.
     */
    struct Rig
    {
            Wire        wire;
            FakeBus     bus0 { wire, 0, 1 };
            FakeBus     bus1 { wire, 1, 0 };
            BenchEngine responder { bus0, g_log[0], LOG_SIZE };
            BenchEngine tester { bus1, g_log[1], LOG_SIZE };

            Rig()
            {
                g_now_us = 1000;
                responder.clear();
                tester.clear();
                responder.add_module(0x7E0, 0x7E8);
                responder.add_module(0x7E1, 0x7E9);
                tester.add_module(0x7E0, 0x7E8);
                tester.add_module(0x7E1, 0x7E9);
            }

            BenchEngine &engine(int i)
            {
                return i == 0 ? responder : tester;
            }

            /**
             * Runs `us` microseconds in 50 µs ticks, delivering frames as
             * they arrive.
             */
            void run(uint32_t us)
            {
                uint32_t end = g_now_us + us;
                while (static_cast<int32_t>(end - g_now_us) > 0)
                {
                    g_now_us += 50;
                    deliver();
                    responder.on_tick(g_now_us);
                    tester.on_tick(g_now_us);
                }
            }

            void deliver()
            {
                bool any = true;
                while (any)
                {
                    any = false;
                    for (size_t i = 0; i < wire.in_flight.size(); i++)
                    {
                        if (static_cast<int32_t>(g_now_us - wire.in_flight[i].arrive_us)
                            >= 0)
                        {
                            InFlight f = wire.in_flight[i];
                            wire.in_flight.erase(wire.in_flight.begin()
                                                 + static_cast<long>(i));
                            engine(f.to).on_frame(f.frame, f.arrive_us);
                            any = true;
                            break;
                        }
                    }
                }
            }
    };

    CanFrame frame(uint32_t id, std::initializer_list<uint8_t> bytes)
    {
        CanFrame f {};
        f.id  = id;
        f.len = 8;
        memset(f.data, 0xAA, sizeof(f.data));
        uint8_t i = 0;
        for (uint8_t b : bytes)
        {
            f.data[i++] = b;
        }
        return f;
    }

    /**
     * A responder alone, answering into `wire.sent[0]`.
     */
    struct Lone
    {
            Wire        wire;
            FakeBus     bus { wire, 0, -1 };
            BenchEngine engine { bus, g_log[0], LOG_SIZE };

            Lone()
            {
                g_now_us = 1000;
                engine.clear();
                engine.add_module(0x7E0, 0x7E8);
            }

            std::vector<CanFrame> &sent()
            {
                return wire.sent[0];
            }

            void tick_for(uint32_t us)
            {
                uint32_t end = g_now_us + us;
                while (static_cast<int32_t>(end - g_now_us) > 0)
                {
                    g_now_us += 50;
                    engine.on_tick(g_now_us);
                }
            }
    };

    void test_st_min_decodes_milliseconds_and_microseconds()
    {
        TEST_ASSERT_EQUAL_UINT32(0, core::bench::st_min_to_us(0x00));
        TEST_ASSERT_EQUAL_UINT32(5000, core::bench::st_min_to_us(0x05));
        TEST_ASSERT_EQUAL_UINT32(100, core::bench::st_min_to_us(0xF1));
        TEST_ASSERT_EQUAL_UINT32(900, core::bench::st_min_to_us(0xF9));
        TEST_ASSERT_EQUAL_UINT32(127000, core::bench::st_min_to_us(0x80));
    }

    void test_responder_answers_at_once_with_the_did_length()
    {
        Lone lone;
        lone.engine.set_did_length(0x7E0, 0xF40C, 2);
        lone.engine.set_did_length(0x7E0, 0xF405, 1);
        lone.engine.start(Role::Responder);

        lone.engine.on_frame(frame(0x7E0, { 0x03, 0x22, 0xF4, 0x0C }), g_now_us);
        lone.engine.on_frame(frame(0x7E0, { 0x03, 0x22, 0xF4, 0x05 }), g_now_us);

        /**
         * No latency: answered inside on_frame, before any tick.
         */
        TEST_ASSERT_EQUAL(2, lone.sent().size());
        const CanFrame &a = lone.sent()[0];
        TEST_ASSERT_EQUAL_HEX32(0x7E8, a.id);
        TEST_ASSERT_EQUAL_HEX8(0x05, a.data[0]);
        TEST_ASSERT_EQUAL_HEX8(0x62, a.data[1]);
        TEST_ASSERT_EQUAL_HEX8(0xF4, a.data[2]);
        TEST_ASSERT_EQUAL_HEX8(0x0C, a.data[3]);
        TEST_ASSERT_EQUAL_HEX8(0x04, lone.sent()[1].data[0]);

        /**
         * The values move from one answer to the next.
         */
        lone.engine.on_frame(frame(0x7E0, { 0x03, 0x22, 0xF4, 0x0C }), g_now_us);
        TEST_ASSERT_NOT_EQUAL(a.data[4], lone.sent()[2].data[4]);
        TEST_ASSERT_EQUAL_UINT32(3, lone.engine.counters().exchanges);
    }

    void test_responder_waits_out_each_latency_in_turn()
    {
        Lone lone;
        lone.engine.add_latency_us(2000);
        lone.engine.add_latency_us(5000);
        lone.engine.start(Role::Responder);

        lone.engine.on_frame(frame(0x7E0, { 0x03, 0x22, 0xF4, 0x0C }), g_now_us);
        lone.tick_for(1950);
        TEST_ASSERT_EQUAL(0, lone.sent().size());
        lone.tick_for(100);
        TEST_ASSERT_EQUAL(1, lone.sent().size());

        lone.engine.on_frame(frame(0x7E0, { 0x03, 0x22, 0xF4, 0x0C }), g_now_us);
        lone.tick_for(4950);
        TEST_ASSERT_EQUAL(1, lone.sent().size());
        lone.tick_for(100);
        TEST_ASSERT_EQUAL(2, lone.sent().size());
    }

    void test_responder_sends_a_long_answer_in_blocks()
    {
        Lone lone;
        lone.engine.start(Role::Responder);

        /**
         * Three 2-byte DIDs: 62 + 3 * (2 + 2) = 13 bytes, a first frame
         * and one consecutive frame.
         */
        lone.engine.on_frame(
            frame(0x7E0, { 0x07, 0x22, 0xF4, 0x0C, 0xF4, 0x05, 0x20, 0x6E }),
            g_now_us);
        TEST_ASSERT_EQUAL(1, lone.sent().size());
        TEST_ASSERT_EQUAL_HEX8(0x10, lone.sent()[0].data[0]);
        TEST_ASSERT_EQUAL_HEX8(13, lone.sent()[0].data[1]);
        TEST_ASSERT_EQUAL_HEX8(0x62, lone.sent()[0].data[2]);

        /**
         * Nothing more until the dash's flow control.
         */
        lone.tick_for(1000);
        TEST_ASSERT_EQUAL(1, lone.sent().size());

        lone.engine.on_frame(frame(0x7E0, { 0x30, 0x03, 0x00 }), g_now_us);
        TEST_ASSERT_EQUAL(2, lone.sent().size());
        TEST_ASSERT_EQUAL_HEX8(0x21, lone.sent()[1].data[0]);
        TEST_ASSERT_EQUAL_HEX8(0x05, lone.sent()[1].data[1]);
        TEST_ASSERT_EQUAL_HEX8(0x20, lone.sent()[1].data[4]);
        TEST_ASSERT_EQUAL_HEX8(0x6E, lone.sent()[1].data[5]);
        TEST_ASSERT_EQUAL_UINT32(1, lone.engine.counters().exchanges);
    }

    void test_responder_honours_block_size_and_st_min()
    {
        Lone lone;
        lone.engine.set_forced_length(29); /* 62 + DID + 29 = 32: FF + 4 CF */
        lone.engine.start(Role::Responder);

        lone.engine.on_frame(frame(0x7E0, { 0x03, 0x22, 0xF4, 0x0C }), g_now_us);
        lone.engine.on_frame(frame(0x7E0, { 0x30, 0x03, 0x02 }), g_now_us);

        /**
         * One consecutive frame now, the next two STmin (2 ms) apart,
         * then a stop at the end of the block of 3.
         */
        TEST_ASSERT_EQUAL(2, lone.sent().size());
        lone.tick_for(1900);
        TEST_ASSERT_EQUAL(2, lone.sent().size());
        lone.tick_for(200);
        TEST_ASSERT_EQUAL(3, lone.sent().size());
        lone.tick_for(2000);
        TEST_ASSERT_EQUAL(4, lone.sent().size());
        lone.tick_for(10000);
        TEST_ASSERT_EQUAL(4, lone.sent().size());

        lone.engine.on_frame(frame(0x7E0, { 0x30, 0x03, 0x00 }), g_now_us);
        TEST_ASSERT_EQUAL(5, lone.sent().size());
        TEST_ASSERT_EQUAL_HEX8(0x24, lone.sent()[4].data[0]);
        TEST_ASSERT_EQUAL_UINT32(1, lone.engine.counters().exchanges);
    }

    void test_responder_pends_drops_and_stays_quiet_when_asked()
    {
        Lone lone;
        lone.engine.add_latency_us(1000);
        lone.engine.set_pending_every(2);
        lone.engine.set_drop_every(3);
        lone.engine.start(Role::Responder);

        CanFrame read = frame(0x7E0, { 0x03, 0x22, 0xF4, 0x0C });

        lone.engine.on_frame(read, g_now_us); /* 1: answered */
        lone.tick_for(1100);
        TEST_ASSERT_EQUAL(1, lone.sent().size());

        lone.engine.on_frame(read, g_now_us); /* 2: 0x78, then answered */
        lone.tick_for(1100);
        TEST_ASSERT_EQUAL(2, lone.sent().size());
        TEST_ASSERT_EQUAL_HEX8(0x7F, lone.sent()[1].data[1]);
        TEST_ASSERT_EQUAL_HEX8(0x78, lone.sent()[1].data[3]);
        lone.tick_for(1100);
        TEST_ASSERT_EQUAL(3, lone.sent().size());
        TEST_ASSERT_EQUAL_HEX8(0x62, lone.sent()[2].data[1]);

        lone.engine.on_frame(read, g_now_us); /* 3: dropped */
        lone.tick_for(5000);
        TEST_ASSERT_EQUAL(3, lone.sent().size());

        /**
         * Tester present with the suppress bit set wants no answer;
         * another module's id is none of this engine's business.
         */
        lone.engine.on_frame(frame(0x7E0, { 0x02, 0x3E, 0x80 }), g_now_us);
        lone.engine.on_frame(frame(0x710, { 0x03, 0x22, 0xF4, 0x0C }), g_now_us);
        lone.tick_for(5000);
        TEST_ASSERT_EQUAL(3, lone.sent().size());
        TEST_ASSERT_EQUAL_UINT32(1, lone.engine.counters().pending);
    }

    void test_tester_runs_a_closed_loop_against_the_responder()
    {
        Rig rig;
        const uint16_t dids[] = { 0xF40C };
        TEST_ASSERT_TRUE(rig.tester.add_job(0, 0x7E0, dids, 1));
        rig.responder.add_latency_us(1000);

        rig.responder.start(Role::Responder);
        rig.tester.start(Role::Tester);
        rig.run(100000);

        /**
         * One frame each way (250 µs each) plus 1 ms of latency, give or
         * take a tick: ~1.5-1.6 ms an exchange.
         */
        uint32_t done = rig.tester.counters().exchanges;
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(60, done);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(67, done);
        TEST_ASSERT_EQUAL_UINT32(0, rig.tester.counters().timeouts);
        TEST_ASSERT_UINT32_WITHIN(1, done, rig.responder.counters().exchanges);
    }

    void test_tester_reads_three_and_six_dids_at_once()
    {
        for (uint8_t n : { 3, 6 })
        {
            Rig rig;
            const uint16_t dids[] = { 0xF40C, 0xF405, 0x206E, 0x437C, 0xF411, 0x3E70 };
            TEST_ASSERT_TRUE(rig.tester.add_job(0, 0x7E0, dids, n));
            rig.tester.set_flow_control(3, 0);

            rig.responder.start(Role::Responder);
            rig.tester.start(Role::Tester);
            rig.run(50000);

            TEST_ASSERT_GREATER_THAN_UINT32(5, rig.tester.counters().exchanges);
            TEST_ASSERT_EQUAL_UINT32(0, rig.tester.counters().timeouts);
            TEST_ASSERT_EQUAL_UINT32(0, rig.tester.counters().nrcs);

            /**
             * A 6-DID request is 13 bytes, so it goes out as a first
             * frame; a 3-DID one fits a single frame.
             */
            const CanFrame &first = rig.wire.sent[1][0];
            TEST_ASSERT_EQUAL_HEX8(n == 3 ? 0x07 : 0x10, first.data[0]);
        }
    }

    void test_tester_times_out_on_silence_and_carries_on()
    {
        Rig rig;
        const uint16_t dids[] = { 0xF40C };
        rig.tester.add_job(0, 0x7E0, dids, 1);
        rig.tester.set_timeout_ms(20);
        rig.responder.set_drop_every(2);

        rig.responder.start(Role::Responder);
        rig.tester.start(Role::Tester);
        rig.run(200000);

        TEST_ASSERT_GREATER_THAN_UINT32(0, rig.tester.counters().timeouts);
        TEST_ASSERT_GREATER_THAN_UINT32(0, rig.tester.counters().exchanges);
        TEST_ASSERT_UINT32_WITHIN(
            1, rig.tester.counters().exchanges, rig.tester.counters().timeouts);
    }

    void test_tester_lanes_run_concurrently()
    {
        Rig rig;
        const uint16_t dids[] = { 0xF40C };
        rig.tester.add_job(0, 0x7E0, dids, 1);
        rig.tester.add_job(1, 0x7E1, dids, 1);
        rig.responder.add_latency_us(5000);

        /**
         * A lane is one module; a second module on the same lane is
         * refused.
         */
        TEST_ASSERT_FALSE(rig.tester.add_job(0, 0x7E1, dids, 1));

        rig.responder.start(Role::Responder);
        rig.tester.start(Role::Tester);
        rig.run(100000);

        /**
         * ~5.5 ms an exchange per lane; two lanes in parallel double it.
         */
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(34, rig.tester.counters().exchanges);
    }

    void test_log_records_both_directions_and_counts_drops()
    {
        Wire       wire;
        FakeBus    bus { wire, 0, -1 };
        BenchFrame small[4];
        BenchEngine engine { bus, small, 4 };
        engine.clear();
        engine.add_module(0x7E0, 0x7E8);
        engine.start(Role::Responder);

        g_now_us = 5000;
        engine.on_frame(frame(0x7E0, { 0x03, 0x22, 0xF4, 0x0C }), 4990);

        BenchFrame f;
        TEST_ASSERT_TRUE(engine.read_log(f));
        TEST_ASSERT_EQUAL_UINT8(core::bench::DIR_RX, f.dir);
        TEST_ASSERT_EQUAL_UINT32(4990, f.t_us);
        TEST_ASSERT_EQUAL_HEX32(0x7E0, f.id);
        TEST_ASSERT_TRUE(engine.read_log(f));
        TEST_ASSERT_EQUAL_UINT8(core::bench::DIR_TX, f.dir);
        TEST_ASSERT_EQUAL_UINT32(5000, f.t_us);
        TEST_ASSERT_EQUAL_HEX32(0x7E8, f.id);
        TEST_ASSERT_FALSE(engine.read_log(f));

        /**
         * Three more exchanges, six frames, into a log of four.
         */
        for (int i = 0; i < 3; i++)
        {
            engine.on_frame(frame(0x7E0, { 0x03, 0x22, 0xF4, 0x0C }), g_now_us);
        }
        TEST_ASSERT_EQUAL_UINT32(2, engine.counters().log_drops);
    }
} // namespace

void setUp() {}
void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_st_min_decodes_milliseconds_and_microseconds);
    RUN_TEST(test_responder_answers_at_once_with_the_did_length);
    RUN_TEST(test_responder_waits_out_each_latency_in_turn);
    RUN_TEST(test_responder_sends_a_long_answer_in_blocks);
    RUN_TEST(test_responder_honours_block_size_and_st_min);
    RUN_TEST(test_responder_pends_drops_and_stays_quiet_when_asked);
    RUN_TEST(test_tester_runs_a_closed_loop_against_the_responder);
    RUN_TEST(test_tester_reads_three_and_six_dids_at_once);
    RUN_TEST(test_tester_times_out_on_silence_and_carries_on);
    RUN_TEST(test_tester_lanes_run_concurrently);
    RUN_TEST(test_log_records_both_directions_and_counts_drops);
    return UNITY_END();
}
