/**
 * @file bench_engine.hpp
 * @brief The v4 dash's bench mode: a fake ECU (responder) or a fake dash
 * (tester) fast enough never to be the bottleneck of the device it's
 * measuring, plus a microsecond-stamped log of every frame on the bus.
 * See THROUGHPUT_TEST_PLAN.md.
 *
 * Platform-free so it can be unit-tested on the host. The board drives it
 * from interrupt context -- on_frame() from the CAN receive interrupt,
 * on_tick() from a periodic timer at the same priority, so the two never
 * nest -- and loop() only reads the log and counters back out. The
 * configuration calls are for when it's stopped; the board's glue makes
 * any loop-side call that touches running state with interrupts off.
 *
 * Classic 11-bit ISO-TP only (normal addressing), which is all the MQB
 * diagnostic modules use.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "platform.hpp"

namespace core
{
    namespace bench
    {
        /**
         * One logged frame. `dir` is DIR_RX for a frame another node sent
         * and DIR_TX for one this engine sent; `t_us` is when it arrived
         * or was handed to the controller, on the board's microsecond
         * clock (wraps every ~71 minutes, which the host unwraps).
         */
        struct BenchFrame
        {
                uint32_t t_us;
                uint32_t id;
                uint8_t  len;
                uint8_t  dir;
                uint8_t  data[8];
        };

        constexpr uint8_t DIR_RX = 0;
        constexpr uint8_t DIR_TX = 1;

        /**
         * What the engine needs from the board. send() must keep frames
         * in the order it's called (ISO-TP consecutive frames depend on
         * it) and be callable from interrupt context.
         */
        class IBenchBus
        {
            public:
                virtual ~IBenchBus() = default;

                virtual bool     send(const CanFrame &frame) = 0;
                virtual uint32_t micros() const              = 0;
        };

        enum class Role : uint8_t
        {
            Idle,
            Responder,
            Tester,
            Listen
        };

        /**
         * Running totals since start(), for the bench screen and the BQ
         * command. The host's own analysis works from the frame log; these
         * are only a sanity check.
         */
        struct Counters
        {
                uint32_t rx        = 0;
                uint32_t tx        = 0;
                uint32_t exchanges = 0; /* answered requests, either role */
                uint32_t timeouts  = 0;
                uint32_t nrcs      = 0; /* final negative answers */
                uint32_t pending   = 0; /* 0x78s, sent or received */
                uint32_t log_drops = 0;
        };

        class BenchEngine
        {
            public:
                static constexpr uint8_t  MAX_MODULES      = 16;
                static constexpr uint8_t  MAX_LANES        = 8;
                static constexpr uint8_t  MAX_JOBS         = 32;
                static constexpr uint8_t  MAX_DIDS_PER_JOB = 6;
                static constexpr uint8_t  MAX_DID_LENGTHS  = 96;
                static constexpr uint16_t MAX_LATENCIES    = 256;

                /**
                 * Longest ISO-TP message either side assembles: a 6-DID
                 * answer of 4-byte values is 37 bytes, so this leaves
                 * room for longer DIDs.
                 */
                static constexpr uint8_t MAX_SDU = 128;

                /**
                 * How long a multi-frame transfer waits for the other
                 * side's flow control before giving up.
                 */
                static constexpr uint32_t FC_TIMEOUT_US = 1000000;

                /**
                 * @param bus      The board's bus; must outlive this.
                 * @param log      Frame log storage, `log_size` entries.
                 * @param log_size A power of two.
                 */
                BenchEngine(IBenchBus &bus, BenchFrame *log, uint32_t log_size);

                /*--------------------------------------------------------
                 * Configuration -- only while stopped (role() == Idle)
                 *------------------------------------------------------*/

                /**
                 * Forgets every module, DID length, latency, job and
                 * option, back to the defaults below.
                 */
                void clear();

                /**
                 * A module: the id it's asked on and the id it answers
                 * on.
                 *
                 * @returns False if the table is full.
                 */
                bool add_module(uint16_t tx_id, uint16_t rx_id);

                /**
                 * Responder: the data length a DID answers with on one
                 * module. A DID with no entry answers with the default.
                 *
                 * @returns False for an unknown module or a full table.
                 */
                bool set_did_length(uint16_t tx_id, uint16_t did, uint8_t len);

                /**
                 * Responder: data bytes per DID when it has no entry of
                 * its own (default 2).
                 */
                void set_default_length(uint8_t len)
                {
                    default_len_ = len == 0 ? 1 : len;
                }

                /**
                 * Responder: every DID answers with this many data bytes,
                 * overriding the table -- for forcing multi-frame
                 * answers. 0 turns it off.
                 */
                void set_forced_length(uint8_t len)
                {
                    forced_len_ = len;
                }

                /**
                 * Responder: one more answer latency. Answers take each
                 * latency in turn, round and round; none at all is 0.
                 *
                 * @returns False if the list is full.
                 */
                bool add_latency_us(uint32_t us);

                /**
                 * Responder: every Nth request is answered 0x78
                 * (response pending) first, then for real a latency
                 * later. 0 never does.
                 */
                void set_pending_every(uint16_t n)
                {
                    pending_every_ = n;
                }

                /**
                 * Responder: every Nth request goes unanswered. 0 never
                 * does.
                 */
                void set_drop_every(uint16_t n)
                {
                    drop_every_ = n;
                }

                /**
                 * Tester: the flow control sent for a multi-frame answer
                 * (default 0/0: send everything, no gap).
                 */
                void set_flow_control(uint8_t block_size, uint8_t st_min)
                {
                    fc_bs_    = block_size;
                    fc_stmin_ = st_min;
                }

                /**
                 * Tester: how long a request waits for its answer
                 * (default 100 ms). A 0x78 restarts it.
                 */
                void set_timeout_ms(uint32_t ms)
                {
                    timeout_us_ = ms * 1000;
                }

                /**
                 * Tester: pause between an answer and a lane's next
                 * request (default 0: closed loop, back to back).
                 */
                void set_gap_us(uint32_t us)
                {
                    gap_us_ = us;
                }

                /**
                 * Tester: one request a lane repeats -- a read of 1 to
                 * MAX_DIDS_PER_JOB DIDs from one module. A lane cycles
                 * through its jobs in the order added, one request in
                 * flight at a time; lanes run concurrently. Every job on
                 * a lane must be for the same module.
                 *
                 * @returns False for an unknown module, a bad lane or DID
                 * count, a module other than the lane's, or a full table.
                 */
                bool add_job(uint8_t         lane,
                             uint16_t        tx_id,
                             const uint16_t *dids,
                             uint8_t         num_dids);

                /*--------------------------------------------------------
                 * Running
                 *------------------------------------------------------*/

                /**
                 * Clears the counters and starts `role`. The tester's
                 * lanes send their first requests on the next tick.
                 */
                void start(Role role);

                void stop();

                Role role() const
                {
                    return role_;
                }

                /**
                 * Interrupt context: every frame received, with its
                 * arrival time.
                 */
                void on_frame(const CanFrame &frame, uint32_t t_us);

                /**
                 * Interrupt context: the periodic timer -- deadlines,
                 * delayed answers, paced consecutive frames.
                 */
                void on_tick(uint32_t t_us);

                /**
                 * Sends one frame as is, logged like any other -- for
                 * one-off probes from the host. Ignored unless running in
                 * a transmitting role.
                 */
                bool send_raw(const CanFrame &frame);

                /**
                 * Loop context: the oldest logged frame not yet read.
                 */
                bool read_log(BenchFrame &out);

                const Counters &counters() const
                {
                    return counters_;
                }

            private:
                struct Module
                {
                        uint16_t tx_id = 0;
                        uint16_t rx_id = 0;
                };

                struct DidLength
                {
                        uint8_t  module = 0;
                        uint16_t did    = 0;
                        uint8_t  len    = 0;
                };

                struct Job
                {
                        uint8_t  lane     = 0;
                        uint8_t  num_dids = 0;
                        uint16_t dids[MAX_DIDS_PER_JOB] {};
                };

                /**
                 * One direction of an ISO-TP transfer in progress: a
                 * multi-frame message going out, or one coming in.
                 */
                struct Transfer
                {
                        uint8_t  buf[MAX_SDU] {};
                        uint8_t  total     = 0;
                        uint8_t  done      = 0;
                        uint8_t  seq       = 0;
                        uint8_t  bs        = 0; /* frames per block, 0 = all */
                        uint8_t  in_block  = 0;
                        uint32_t st_min_us = 0;
                        uint32_t next_us   = 0;
                };

                enum class RespState : uint8_t
                {
                    Idle,
                    Receiving,  /* a multi-frame request coming in */
                    Delaying,   /* answer due at due_us */
                    WaitingFc,  /* first frame out, waiting on the tester */
                    SendingCf   /* consecutive frames going out */
                };

                struct Responder
                {
                        RespState state    = RespState::Idle;
                        bool      pending  = false; /* 0x78 sent first */
                        uint32_t  due_us   = 0;
                        Transfer  in;
                        Transfer  out;
                };

                enum class LaneState : uint8_t
                {
                    Unused,
                    Ready,      /* next request due at due_us */
                    WaitingFc,  /* multi-frame request: waiting on the module */
                    SendingCf,  /* multi-frame request going out */
                    Waiting,    /* request out, answer due by due_us */
                    Receiving   /* multi-frame answer coming in */
                };

                struct Lane
                {
                        LaneState state  = LaneState::Unused;
                        uint8_t   module = 0;
                        uint8_t   next   = 0; /* index into jobs_, this lane's */
                        uint32_t  due_us = 0;
                        Transfer  out;
                        Transfer  in;
                };

                int  find_module_by_tx(uint32_t id) const;
                int  find_module_by_rx(uint32_t id) const;
                bool send(const CanFrame &frame);
                void log(const CanFrame &frame, uint8_t dir, uint32_t t_us);

                uint8_t  did_length(uint8_t module, uint16_t did) const;
                uint32_t next_latency();

                void respond_to(uint8_t module, const uint8_t *sdu, uint8_t len, uint32_t t_us);
                void responder_send_answer(uint8_t module, uint32_t t_us);
                void responder_on_fc(uint8_t module, const CanFrame &frame, uint32_t t_us);
                void responder_on_request_frame(uint8_t module, const CanFrame &frame, uint32_t t_us);
                void responder_tick(uint8_t module, uint32_t t_us);

                void lane_send_next(uint8_t lane, uint32_t t_us);
                void lane_finish(uint8_t lane, uint32_t t_us);
                void lane_on_frame(uint8_t lane, const CanFrame &frame, uint32_t t_us);
                void lane_tick(uint8_t lane, uint32_t t_us);

                /**
                 * Sends `t`'s consecutive frames: as many as its block
                 * allows when STmin is 0, else one each time STmin has
                 * passed. A caller seeing false with the block used up
                 * waits for the next flow control.
                 *
                 * @returns True once the whole message is out.
                 */
                bool pump_consecutive(Transfer &t, uint16_t id, uint32_t t_us);

                void send_flow_control(uint16_t id, uint8_t status, uint8_t bs, uint8_t st_min);
                void build_answer(uint8_t module, const uint8_t *sdu, uint8_t len, Transfer &out);

                IBenchBus  &bus_;
                BenchFrame *log_;
                uint32_t    log_mask_;

                /**
                 * Single producer (interrupt context, or loop with
                 * interrupts off), single consumer (loop): each index is
                 * written by one side only.
                 */
                volatile uint32_t log_head_ = 0;
                volatile uint32_t log_tail_ = 0;

                Role     role_ = Role::Idle;
                Counters counters_ {};

                Module    modules_[MAX_MODULES] {};
                uint8_t   num_modules_ = 0;
                DidLength did_lengths_[MAX_DID_LENGTHS] {};
                uint8_t   num_did_lengths_ = 0;
                uint32_t  latencies_[MAX_LATENCIES] {};
                uint16_t  num_latencies_ = 0;
                uint16_t  latency_next_  = 0;
                Job       jobs_[MAX_JOBS] {};
                uint8_t   num_jobs_ = 0;

                uint8_t  default_len_   = 2;
                uint8_t  forced_len_    = 0;
                uint16_t pending_every_ = 0;
                uint16_t drop_every_    = 0;
                uint32_t requests_seen_ = 0;
                uint8_t  fc_bs_         = 0;
                uint8_t  fc_stmin_      = 0;
                uint32_t timeout_us_    = 100000;
                uint32_t gap_us_        = 0;

                /**
                 * Changes with every answer, so the device under test's
                 * on-change logging always has something to write.
                 */
                uint8_t sweep_ = 0;

                Responder responders_[MAX_MODULES] {};
                Lane      lanes_[MAX_LANES] {};
        };

        /**
         * ISO-TP's STmin byte as microseconds: 0-127 ms, 0xF1-0xF9 100-900
         * µs, anything reserved read as the slowest legal value.
         */
        uint32_t st_min_to_us(uint8_t st_min);

    } // namespace bench
} // namespace core
