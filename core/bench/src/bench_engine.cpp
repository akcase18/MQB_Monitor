#include "bench_engine.hpp"

#include <string.h>

#include "mem_placement.hpp"

/**
 * Everything here is FLASHMEM, the per-frame paths included: on the Teensy
 * RAM1 code sits just under a 32 KB ITCM block boundary, and bench mode
 * only runs with the dashboard paused, so this small a working set stays
 * resident in the instruction cache.
 */

namespace core
{
    namespace bench
    {
        namespace
        {
            constexpr uint8_t PCI_SINGLE      = 0x0;
            constexpr uint8_t PCI_FIRST       = 0x1;
            constexpr uint8_t PCI_CONSECUTIVE = 0x2;
            constexpr uint8_t PCI_FLOW        = 0x3;

            constexpr uint8_t FC_CONTINUE = 0x0;
            constexpr uint8_t FC_WAIT     = 0x1;
            constexpr uint8_t FC_OVERFLOW = 0x2;

            constexpr uint8_t SID_READ_DID       = 0x22;
            constexpr uint8_t SID_SESSION        = 0x10;
            constexpr uint8_t SID_TESTER_PRESENT = 0x3E;
            constexpr uint8_t SID_NEGATIVE       = 0x7F;
            constexpr uint8_t POSITIVE_OFFSET    = 0x40;

            constexpr uint8_t NRC_NOT_SUPPORTED    = 0x11;
            constexpr uint8_t NRC_RESPONSE_PENDING = 0x78;

            /**
             * Unused bytes of a frame; every frame goes out 8 long.
             */
            constexpr uint8_t PAD = 0xAA;

            bool reached(uint32_t now_us, uint32_t due_us)
            {
                return static_cast<int32_t>(now_us - due_us) >= 0;
            }

            CanFrame padded_frame(uint32_t id)
            {
                CanFrame frame;
                frame.id  = id;
                frame.len = 8;
                memset(frame.data, PAD, sizeof(frame.data));
                return frame;
            }
        } // namespace

        FLASHMEM uint32_t st_min_to_us(uint8_t st_min)
        {
            if (st_min <= 0x7F)
            {
                return static_cast<uint32_t>(st_min) * 1000;
            }
            if (st_min >= 0xF1 && st_min <= 0xF9)
            {
                return static_cast<uint32_t>(st_min - 0xF0) * 100;
            }
            return 127000;
        }

        FLASHMEM BenchEngine::BenchEngine(IBenchBus  &bus,
                                          BenchFrame *log,
                                          uint32_t    log_size)
            : bus_(bus)
            , log_(log)
            , log_mask_(log_size - 1)
        {
        }

        /*----------------------------------------------------------------------
         * Configuration
         *--------------------------------------------------------------------*/

        FLASHMEM void BenchEngine::clear()
        {
            num_modules_     = 0;
            num_did_lengths_ = 0;
            num_latencies_   = 0;
            latency_next_    = 0;
            num_jobs_        = 0;
            default_len_     = 2;
            forced_len_      = 0;
            pending_every_   = 0;
            drop_every_      = 0;
            fc_bs_           = 0;
            fc_stmin_        = 0;
            timeout_us_      = 100000;
            gap_us_          = 0;

            for (Responder &r : responders_)
            {
                r = Responder {};
            }
            for (Lane &lane : lanes_)
            {
                lane = Lane {};
            }
        }

        FLASHMEM bool BenchEngine::add_module(uint16_t tx_id, uint16_t rx_id)
        {
            if (num_modules_ >= MAX_MODULES || find_module_by_tx(tx_id) >= 0)
            {
                return false;
            }
            modules_[num_modules_++] = Module { tx_id, rx_id };
            return true;
        }

        FLASHMEM bool BenchEngine::set_did_length(uint16_t tx_id,
                                                  uint16_t did,
                                                  uint8_t  len)
        {
            int module = find_module_by_tx(tx_id);
            if (module < 0 || len == 0)
            {
                return false;
            }

            for (uint8_t i = 0; i < num_did_lengths_; i++)
            {
                DidLength &entry = did_lengths_[i];
                if (entry.module == module && entry.did == did)
                {
                    entry.len = len;
                    return true;
                }
            }

            if (num_did_lengths_ >= MAX_DID_LENGTHS)
            {
                return false;
            }
            did_lengths_[num_did_lengths_++]
                = DidLength { static_cast<uint8_t>(module), did, len };
            return true;
        }

        FLASHMEM bool BenchEngine::add_latency_us(uint32_t us)
        {
            if (num_latencies_ >= MAX_LATENCIES)
            {
                return false;
            }
            latencies_[num_latencies_++] = us;
            return true;
        }

        FLASHMEM bool BenchEngine::add_job(uint8_t         lane,
                                           uint16_t        tx_id,
                                           const uint16_t *dids,
                                           uint8_t         num_dids)
        {
            int module = find_module_by_tx(tx_id);
            if (module < 0 || lane >= MAX_LANES || num_dids == 0
                || num_dids > MAX_DIDS_PER_JOB || num_jobs_ >= MAX_JOBS)
            {
                return false;
            }

            Lane &l = lanes_[lane];
            if (l.state == LaneState::Unused)
            {
                l.module = static_cast<uint8_t>(module);
                l.state  = LaneState::Ready;
            }
            else if (l.module != module)
            {
                return false;
            }

            Job &job     = jobs_[num_jobs_++];
            job.lane     = lane;
            job.num_dids = num_dids;
            memcpy(job.dids, dids, num_dids * sizeof(uint16_t));
            return true;
        }

        /*----------------------------------------------------------------------
         * Running
         *--------------------------------------------------------------------*/

        FLASHMEM void BenchEngine::start(Role role)
        {
            uint32_t now = bus_.micros();

            counters_      = Counters {};
            requests_seen_ = 0;
            latency_next_  = 0;

            for (Responder &r : responders_)
            {
                r = Responder {};
            }
            for (Lane &lane : lanes_)
            {
                if (lane.state != LaneState::Unused)
                {
                    lane.state  = LaneState::Ready;
                    lane.next   = 0;
                    lane.due_us = now;
                }
            }

            role_ = role;
        }

        FLASHMEM void BenchEngine::stop()
        {
            role_ = Role::Idle;
        }

        FLASHMEM void BenchEngine::on_frame(const CanFrame &frame, uint32_t t_us)
        {
            log(frame, DIR_RX, t_us);
            counters_.rx++;

            if (frame.len == 0)
            {
                return;
            }
            uint8_t pci = frame.data[0] >> 4;

            if (role_ == Role::Responder)
            {
                int module = find_module_by_tx(frame.id);
                if (module < 0)
                {
                    return;
                }
                if (pci == PCI_SINGLE)
                {
                    uint8_t len = frame.data[0] & 0x0F;
                    if (len > 0 && len < frame.len)
                    {
                        respond_to(static_cast<uint8_t>(module),
                                   &frame.data[1],
                                   len,
                                   t_us);
                    }
                }
                else if (pci == PCI_FLOW)
                {
                    responder_on_fc(static_cast<uint8_t>(module), frame, t_us);
                }
                else
                {
                    responder_on_request_frame(
                        static_cast<uint8_t>(module), frame, t_us);
                }
                return;
            }

            if (role_ == Role::Tester)
            {
                int module = find_module_by_rx(frame.id);
                if (module < 0)
                {
                    return;
                }

                /**
                 * The first lane on this module waiting for this kind of
                 * frame takes it. Two lanes on one module (test C4b) is
                 * the only way more than one could be.
                 */
                for (uint8_t i = 0; i < MAX_LANES; i++)
                {
                    const Lane &lane = lanes_[i];
                    if (lane.state == LaneState::Unused || lane.module != module)
                    {
                        continue;
                    }

                    bool wants
                        = (pci == PCI_FLOW && lane.state == LaneState::WaitingFc)
                          || ((pci == PCI_SINGLE || pci == PCI_FIRST)
                              && lane.state == LaneState::Waiting)
                          || (pci == PCI_CONSECUTIVE
                              && lane.state == LaneState::Receiving);
                    if (wants)
                    {
                        lane_on_frame(i, frame, t_us);
                        return;
                    }
                }
            }
        }

        FLASHMEM void BenchEngine::on_tick(uint32_t t_us)
        {
            if (role_ == Role::Responder)
            {
                for (uint8_t i = 0; i < num_modules_; i++)
                {
                    if (responders_[i].state != RespState::Idle)
                    {
                        responder_tick(i, t_us);
                    }
                }
            }
            else if (role_ == Role::Tester)
            {
                for (uint8_t i = 0; i < MAX_LANES; i++)
                {
                    if (lanes_[i].state != LaneState::Unused)
                    {
                        lane_tick(i, t_us);
                    }
                }
            }
        }

        FLASHMEM bool BenchEngine::send_raw(const CanFrame &frame)
        {
            if (role_ != Role::Responder && role_ != Role::Tester)
            {
                return false;
            }
            return send(frame);
        }

        FLASHMEM bool BenchEngine::read_log(BenchFrame &out)
        {
            uint32_t tail = log_tail_;
            if (tail == log_head_)
            {
                return false;
            }
            out       = log_[tail & log_mask_];
            log_tail_ = tail + 1;
            return true;
        }

        /*----------------------------------------------------------------------
         * Shared
         *--------------------------------------------------------------------*/

        FLASHMEM int BenchEngine::find_module_by_tx(uint32_t id) const
        {
            for (uint8_t i = 0; i < num_modules_; i++)
            {
                if (modules_[i].tx_id == id)
                {
                    return i;
                }
            }
            return -1;
        }

        FLASHMEM int BenchEngine::find_module_by_rx(uint32_t id) const
        {
            for (uint8_t i = 0; i < num_modules_; i++)
            {
                if (modules_[i].rx_id == id)
                {
                    return i;
                }
            }
            return -1;
        }

        FLASHMEM bool BenchEngine::send(const CanFrame &frame)
        {
            bool ok = bus_.send(frame);
            log(frame, DIR_TX, bus_.micros());
            counters_.tx++;
            return ok;
        }

        FLASHMEM void BenchEngine::log(const CanFrame &frame,
                                       uint8_t         dir,
                                       uint32_t        t_us)
        {
            uint32_t head = log_head_;
            if (head - log_tail_ > log_mask_)
            {
                counters_.log_drops++;
                return;
            }

            BenchFrame &entry = log_[head & log_mask_];
            entry.t_us        = t_us;
            entry.id          = frame.id;
            entry.len         = frame.len > 8 ? 8 : frame.len;
            entry.dir         = dir;
            memcpy(entry.data, frame.data, sizeof(entry.data));

            log_head_ = head + 1;
        }

        FLASHMEM void BenchEngine::send_flow_control(uint16_t id,
                                                     uint8_t  status,
                                                     uint8_t  bs,
                                                     uint8_t  st_min)
        {
            CanFrame frame = padded_frame(id);
            frame.data[0]  = static_cast<uint8_t>((PCI_FLOW << 4) | status);
            frame.data[1]  = bs;
            frame.data[2]  = st_min;
            send(frame);
        }

        FLASHMEM bool BenchEngine::pump_consecutive(Transfer &t,
                                                    uint16_t  id,
                                                    uint32_t  t_us)
        {
            while (t.done < t.total)
            {
                if (t.bs != 0 && t.in_block >= t.bs)
                {
                    return false;
                }
                if (t.st_min_us != 0 && !reached(t_us, t.next_us))
                {
                    return false;
                }

                CanFrame frame = padded_frame(id);
                frame.data[0]
                    = static_cast<uint8_t>((PCI_CONSECUTIVE << 4) | (t.seq & 0x0F));
                uint8_t n = static_cast<uint8_t>(t.total - t.done);
                if (n > 7)
                {
                    n = 7;
                }
                memcpy(&frame.data[1], &t.buf[t.done], n);
                send(frame);

                t.done = static_cast<uint8_t>(t.done + n);
                t.seq  = static_cast<uint8_t>((t.seq + 1) & 0x0F);
                t.in_block++;
                if (t.st_min_us != 0)
                {
                    t.next_us = t_us + t.st_min_us;
                }
            }
            return true;
        }

        /*----------------------------------------------------------------------
         * Responder
         *--------------------------------------------------------------------*/

        FLASHMEM uint8_t BenchEngine::did_length(uint8_t module, uint16_t did) const
        {
            if (forced_len_ != 0)
            {
                return forced_len_;
            }
            for (uint8_t i = 0; i < num_did_lengths_; i++)
            {
                if (did_lengths_[i].module == module && did_lengths_[i].did == did)
                {
                    return did_lengths_[i].len;
                }
            }
            return default_len_;
        }

        FLASHMEM uint32_t BenchEngine::next_latency()
        {
            if (num_latencies_ == 0)
            {
                return 0;
            }
            uint32_t us   = latencies_[latency_next_];
            latency_next_ = static_cast<uint16_t>((latency_next_ + 1) % num_latencies_);
            return us;
        }

        FLASHMEM void BenchEngine::build_answer(uint8_t        module,
                                                const uint8_t *sdu,
                                                uint8_t        len,
                                                Transfer      &out)
        {
            out       = Transfer {};
            uint8_t sid = sdu[0];

            if (sid == SID_READ_DID && len >= 3 && (len - 1) % 2 == 0)
            {
                out.buf[0] = SID_READ_DID + POSITIVE_OFFSET;
                uint8_t n  = 1;
                for (uint8_t i = 1; i + 1 < len; i += 2)
                {
                    uint16_t did  = static_cast<uint16_t>((sdu[i] << 8) | sdu[i + 1]);
                    uint8_t  dlen = did_length(module, did);
                    if (n + 2 + dlen > MAX_SDU)
                    {
                        break;
                    }
                    out.buf[n++] = sdu[i];
                    out.buf[n++] = sdu[i + 1];
                    for (uint8_t b = 0; b < dlen; b++)
                    {
                        out.buf[n++] = static_cast<uint8_t>(sweep_ + b * 37);
                    }
                }
                out.total = n;
                sweep_++;
            }
            else if (sid == SID_TESTER_PRESENT && len >= 2)
            {
                if ((sdu[1] & 0x80) == 0)
                {
                    out.buf[0] = SID_TESTER_PRESENT + POSITIVE_OFFSET;
                    out.buf[1] = sdu[1];
                    out.total  = 2;
                }
            }
            else if (sid == SID_SESSION && len >= 2)
            {
                /**
                 * P2 50 ms, P2* 5000 ms: what VW modules report.
                 */
                const uint8_t answer[] = { SID_SESSION + POSITIVE_OFFSET,
                                           sdu[1],
                                           0x00,
                                           0x32,
                                           0x01,
                                           0xF4 };
                memcpy(out.buf, answer, sizeof(answer));
                out.total = sizeof(answer);
            }
            else
            {
                out.buf[0] = SID_NEGATIVE;
                out.buf[1] = sid;
                out.buf[2] = NRC_NOT_SUPPORTED;
                out.total  = 3;
            }
        }

        FLASHMEM void BenchEngine::respond_to(uint8_t        module,
                                              const uint8_t *sdu,
                                              uint8_t        len,
                                              uint32_t       t_us)
        {
            Responder &r = responders_[module];
            r.state      = RespState::Idle;

            requests_seen_++;
            if (drop_every_ != 0 && requests_seen_ % drop_every_ == 0)
            {
                return;
            }

            build_answer(module, sdu, len, r.out);
            if (r.out.total == 0)
            {
                return; /* suppressed positive response: nothing to say */
            }

            r.pending = pending_every_ != 0 && requests_seen_ % pending_every_ == 0;
            r.state   = RespState::Delaying;

            uint32_t latency = next_latency();
            if (latency == 0 && !r.pending)
            {
                responder_send_answer(module, t_us);
            }
            else
            {
                r.due_us = t_us + latency;
            }
        }

        FLASHMEM void BenchEngine::responder_send_answer(uint8_t module, uint32_t t_us)
        {
            Responder &r  = responders_[module];
            uint16_t   id = modules_[module].rx_id;

            if (r.pending)
            {
                CanFrame frame = padded_frame(id);
                frame.data[0]  = 3;
                frame.data[1]  = SID_NEGATIVE;
                frame.data[2]  = SID_READ_DID;
                frame.data[3]  = NRC_RESPONSE_PENDING;
                send(frame);

                counters_.pending++;
                r.pending = false;
                r.due_us  = t_us + next_latency();
                return;
            }

            CanFrame frame = padded_frame(id);
            if (r.out.total <= 7)
            {
                frame.data[0] = r.out.total;
                memcpy(&frame.data[1], r.out.buf, r.out.total);
                send(frame);

                r.state = RespState::Idle;
                if (r.out.buf[0] == SID_NEGATIVE)
                {
                    counters_.nrcs++;
                }
                else
                {
                    counters_.exchanges++;
                }
                return;
            }

            frame.data[0] = static_cast<uint8_t>(PCI_FIRST << 4);
            frame.data[1] = r.out.total;
            memcpy(&frame.data[2], r.out.buf, 6);
            send(frame);

            r.out.done = 6;
            r.out.seq  = 1;
            r.state    = RespState::WaitingFc;
            r.due_us   = t_us + FC_TIMEOUT_US;
        }

        FLASHMEM void BenchEngine::responder_on_fc(uint8_t         module,
                                                   const CanFrame &frame,
                                                   uint32_t        t_us)
        {
            Responder &r = responders_[module];
            if (r.state != RespState::WaitingFc)
            {
                return;
            }

            uint8_t status = frame.data[0] & 0x0F;
            if (status == FC_WAIT)
            {
                r.due_us = t_us + FC_TIMEOUT_US;
                return;
            }
            if (status != FC_CONTINUE)
            {
                r.state = RespState::Idle; /* overflow: the tester gave up */
                return;
            }

            r.out.bs        = frame.data[1];
            r.out.st_min_us = st_min_to_us(frame.data[2]);
            r.out.in_block  = 0;
            r.out.next_us   = t_us;
            r.state         = RespState::SendingCf;
            responder_tick(module, t_us);
        }

        /**
         * A multi-frame request -- a read of 4 or more DIDs: flow control
         * straight back (no limits), then the request is answered like any
         * other once its last consecutive frame is in.
         */
        FLASHMEM void BenchEngine::responder_on_request_frame(uint8_t         module,
                                                              const CanFrame &frame,
                                                              uint32_t        t_us)
        {
            Responder &r   = responders_[module];
            uint8_t    pci = frame.data[0] >> 4;
            uint16_t   id  = modules_[module].rx_id;

            if (pci == PCI_FIRST)
            {
                uint16_t total
                    = static_cast<uint16_t>(((frame.data[0] & 0x0F) << 8) | frame.data[1]);
                if (total > MAX_SDU || total < 8)
                {
                    send_flow_control(id, FC_OVERFLOW, 0, 0);
                    r.state = RespState::Idle;
                    return;
                }

                r.in       = Transfer {};
                r.in.total = static_cast<uint8_t>(total);
                memcpy(r.in.buf, &frame.data[2], 6);
                r.in.done = 6;
                r.in.seq  = 1;
                r.state   = RespState::Receiving;
                r.due_us  = t_us + FC_TIMEOUT_US;
                send_flow_control(id, FC_CONTINUE, 0, 0);
                return;
            }

            if (pci != PCI_CONSECUTIVE || r.state != RespState::Receiving)
            {
                return;
            }
            if ((frame.data[0] & 0x0F) != r.in.seq)
            {
                r.state = RespState::Idle; /* a frame went missing */
                return;
            }

            uint8_t n = static_cast<uint8_t>(r.in.total - r.in.done);
            if (n > 7)
            {
                n = 7;
            }
            memcpy(&r.in.buf[r.in.done], &frame.data[1], n);
            r.in.done = static_cast<uint8_t>(r.in.done + n);
            r.in.seq  = static_cast<uint8_t>((r.in.seq + 1) & 0x0F);

            if (r.in.done >= r.in.total)
            {
                respond_to(module, r.in.buf, r.in.total, t_us);
            }
        }

        FLASHMEM void BenchEngine::responder_tick(uint8_t module, uint32_t t_us)
        {
            Responder &r = responders_[module];

            switch (r.state)
            {
                case RespState::Delaying:
                    if (reached(t_us, r.due_us))
                    {
                        responder_send_answer(module, t_us);
                    }
                    break;

                case RespState::Receiving:
                case RespState::WaitingFc:
                    if (reached(t_us, r.due_us))
                    {
                        r.state = RespState::Idle;
                    }
                    break;

                case RespState::SendingCf:
                    if (pump_consecutive(r.out, modules_[module].rx_id, t_us))
                    {
                        r.state = RespState::Idle;
                        counters_.exchanges++;
                    }
                    else if (r.out.bs != 0 && r.out.in_block >= r.out.bs)
                    {
                        r.state  = RespState::WaitingFc;
                        r.due_us = t_us + FC_TIMEOUT_US;
                    }
                    break;

                case RespState::Idle:
                default:
                    break;
            }
        }

        /*----------------------------------------------------------------------
         * Tester
         *--------------------------------------------------------------------*/

        FLASHMEM void BenchEngine::lane_send_next(uint8_t lane, uint32_t t_us)
        {
            Lane &l = lanes_[lane];

            /**
             * This lane's next job, round and round in the order added.
             */
            int job = -1;
            for (uint8_t k = 0; k < num_jobs_; k++)
            {
                uint8_t j = static_cast<uint8_t>((l.next + k) % num_jobs_);
                if (jobs_[j].lane == lane)
                {
                    job = j;
                    break;
                }
            }
            if (job < 0)
            {
                l.state = LaneState::Unused;
                return;
            }
            l.next = static_cast<uint8_t>((job + 1) % num_jobs_);

            const Job &j   = jobs_[job];
            Transfer  &out = l.out;
            out            = Transfer {};
            out.buf[0]     = SID_READ_DID;
            out.total      = 1;
            for (uint8_t d = 0; d < j.num_dids; d++)
            {
                out.buf[out.total++] = static_cast<uint8_t>(j.dids[d] >> 8);
                out.buf[out.total++] = static_cast<uint8_t>(j.dids[d]);
            }

            uint16_t id    = modules_[l.module].tx_id;
            CanFrame frame = padded_frame(id);
            if (out.total <= 7)
            {
                frame.data[0] = out.total;
                memcpy(&frame.data[1], out.buf, out.total);
                send(frame);

                l.state  = LaneState::Waiting;
                l.due_us = t_us + timeout_us_;
                return;
            }

            frame.data[0] = static_cast<uint8_t>(PCI_FIRST << 4);
            frame.data[1] = out.total;
            memcpy(&frame.data[2], out.buf, 6);
            send(frame);

            out.done = 6;
            out.seq  = 1;
            l.state  = LaneState::WaitingFc;
            l.due_us = t_us + FC_TIMEOUT_US;
        }

        FLASHMEM void BenchEngine::lane_finish(uint8_t lane, uint32_t t_us)
        {
            Lane &l  = lanes_[lane];
            l.state  = LaneState::Ready;
            l.due_us = t_us + gap_us_;
            if (gap_us_ == 0)
            {
                lane_send_next(lane, t_us);
            }
        }

        FLASHMEM void BenchEngine::lane_on_frame(uint8_t         lane,
                                                 const CanFrame &frame,
                                                 uint32_t        t_us)
        {
            Lane   &l   = lanes_[lane];
            uint8_t pci = frame.data[0] >> 4;
            uint16_t id = modules_[l.module].tx_id;

            if (pci == PCI_FLOW)
            {
                uint8_t status = frame.data[0] & 0x0F;
                if (status == FC_WAIT)
                {
                    l.due_us = t_us + FC_TIMEOUT_US;
                    return;
                }
                if (status != FC_CONTINUE)
                {
                    counters_.timeouts++; /* refused: counts as unanswered */
                    lane_finish(lane, t_us);
                    return;
                }
                l.out.bs        = frame.data[1];
                l.out.st_min_us = st_min_to_us(frame.data[2]);
                l.out.in_block  = 0;
                l.out.next_us   = t_us;
                l.state         = LaneState::SendingCf;
                lane_tick(lane, t_us);
                return;
            }

            if (pci == PCI_SINGLE)
            {
                uint8_t len = frame.data[0] & 0x0F;
                if (len == 0 || len >= frame.len)
                {
                    return;
                }
                const uint8_t *sdu = &frame.data[1];
                if (sdu[0] == SID_NEGATIVE && len >= 3)
                {
                    if (sdu[2] == NRC_RESPONSE_PENDING)
                    {
                        counters_.pending++;
                        l.due_us = t_us + timeout_us_;
                        return;
                    }
                    counters_.nrcs++;
                    lane_finish(lane, t_us);
                }
                else if (sdu[0] == SID_READ_DID + POSITIVE_OFFSET)
                {
                    counters_.exchanges++;
                    lane_finish(lane, t_us);
                }
                return;
            }

            if (pci == PCI_FIRST)
            {
                uint16_t total
                    = static_cast<uint16_t>(((frame.data[0] & 0x0F) << 8) | frame.data[1]);
                if (total > MAX_SDU || total < 8)
                {
                    send_flow_control(id, FC_OVERFLOW, 0, 0);
                    counters_.timeouts++;
                    lane_finish(lane, t_us);
                    return;
                }

                Transfer &in = l.in;
                in           = Transfer {};
                in.total     = static_cast<uint8_t>(total);
                memcpy(in.buf, &frame.data[2], 6);
                in.done = 6;
                in.seq  = 1;

                send_flow_control(id, FC_CONTINUE, fc_bs_, fc_stmin_);
                l.state  = LaneState::Receiving;
                l.due_us = t_us + timeout_us_;
                return;
            }

            if (pci == PCI_CONSECUTIVE)
            {
                Transfer &in = l.in;
                if ((frame.data[0] & 0x0F) != in.seq)
                {
                    counters_.timeouts++; /* a frame went missing */
                    lane_finish(lane, t_us);
                    return;
                }

                uint8_t n = static_cast<uint8_t>(in.total - in.done);
                if (n > 7)
                {
                    n = 7;
                }
                memcpy(&in.buf[in.done], &frame.data[1], n);
                in.done = static_cast<uint8_t>(in.done + n);
                in.seq  = static_cast<uint8_t>((in.seq + 1) & 0x0F);
                in.in_block++;

                if (in.done >= in.total)
                {
                    if (in.buf[0] == SID_NEGATIVE)
                    {
                        counters_.nrcs++;
                    }
                    else
                    {
                        counters_.exchanges++;
                    }
                    lane_finish(lane, t_us);
                    return;
                }

                if (fc_bs_ != 0 && in.in_block >= fc_bs_)
                {
                    in.in_block = 0;
                    send_flow_control(id, FC_CONTINUE, fc_bs_, fc_stmin_);
                }
                l.due_us = t_us + timeout_us_;
            }
        }

        FLASHMEM void BenchEngine::lane_tick(uint8_t lane, uint32_t t_us)
        {
            Lane &l = lanes_[lane];

            switch (l.state)
            {
                case LaneState::Ready:
                    if (reached(t_us, l.due_us))
                    {
                        lane_send_next(lane, t_us);
                    }
                    break;

                case LaneState::WaitingFc:
                case LaneState::Waiting:
                case LaneState::Receiving:
                    if (reached(t_us, l.due_us))
                    {
                        counters_.timeouts++;
                        lane_finish(lane, t_us);
                    }
                    break;

                case LaneState::SendingCf:
                    if (pump_consecutive(l.out, modules_[l.module].tx_id, t_us))
                    {
                        l.state  = LaneState::Waiting;
                        l.due_us = t_us + timeout_us_;
                    }
                    else if (l.out.bs != 0 && l.out.in_block >= l.out.bs)
                    {
                        l.state  = LaneState::WaitingFc;
                        l.due_us = t_us + FC_TIMEOUT_US;
                    }
                    break;

                case LaneState::Unused:
                default:
                    break;
            }
        }

    } // namespace bench
} // namespace core
