/**
 * @file synthetic_can_bus.hpp
 * @brief Board-agnostic synthetic ICanBus: any UDS enhanced-data-service
 * request placed via send() gets an immediate fabricated response queued
 * for the next receive(), standing in for a real ECU so core/can +
 * core/vehicle + gui/ can be exercised without a car.
 *
 * Fake values are deterministic -- a per-signal base derived from
 * byte1/byte2 plus a slow triangular drift off a monotonic response
 * counter, never a RNG -- so a given run still reproduces exactly.
 *
 * Header-only and dependency-free on purpose: it lives at platform/ root
 * rather than under a board directory because more than one board wants
 * it. The simulator wraps it as SimulatorCanBus (its only bus); a real
 * board can swap it in for its hardware CAN driver to stress the display
 * pipeline on a bench with no vehicle attached (see dash_s3_platform.cpp).
 */
#pragma once

#include "platform.hpp"

class SyntheticCanBus : public ICanBus
{
    public:
        /**
         * @param sends_per_step How many responses between one step of
         * the drift ramp. 8 is the simulator's gentle wobble; 1 makes a
         * bench display visibly sweep.
         * @param amplitude Peak drift in raw counts, of a 0-4095 range.
         * 256 (6%) is the simulator's; 2048 swings each value across half
         * its range, enough to walk every panel through its warning
         * threshold.
         */
        explicit SyntheticCanBus(uint32_t sends_per_step = 8,
                                 int32_t  amplitude      = 256)
            : sends_per_step_(sends_per_step == 0 ? 1 : sends_per_step),
              amplitude_(amplitude)
        {
        }

        bool begin(uint32_t tx_id, uint32_t rx_id, uint32_t baud) override
        {
            (void)tx_id;
            (void)baud;
            rx_id_ = rx_id;
            return true;
        }

        bool send(const CanFrame &frame) override
        {
            if (frame.len < 4 || frame.data[1] != UDS_REQUEST_SID
                || count_ >= QUEUE_SIZE)
            {
                return false;
            }

            uint8_t byte1 = frame.data[2];
            uint8_t byte2 = frame.data[3];

            /* A per-signal base, plus a slow triangular sweep so values
             * actually move over time. The sweep is not cosmetic: with a
             * constant response per signal, core::Logger's on-change
             * recording correctly writes nothing at all, which left the
             * entire logging path impossible to exercise in the simulator
             * (and every panel reading frozen). The phase is offset per
             * signal so they don't sweep in lockstep. */
            int32_t base
                = static_cast<int32_t>((byte1 * 173u + byte2 * 37u) % 4096u);

            uint32_t phase = ((tick_++ / sends_per_step_) + byte1 + byte2) % 512u;
            int32_t  ramp  = phase < 256u ? static_cast<int32_t>(phase)
                                          : static_cast<int32_t>(512u - phase);
            int32_t  drift = ((ramp - 128) * amplitude_) / 128; /* -amp..+amp */

            int32_t drifted = base + drift;
            if (drifted < 0)
            {
                drifted = 0;
            }
            else if (drifted > 4095)
            {
                drifted = 4095;
            }
            uint16_t fake_raw = static_cast<uint16_t>(drifted);
            last_fake_raw_    = fake_raw;

            CanFrame &response = queue_[head_];
            response.id        = rx_id_;
            response.len       = 8;
            response.data[0]   = TWO_BYTE_PAYLOAD;
            response.data[1]   = UDS_RESPONSE_SID;
            response.data[2]   = byte1;
            response.data[3]   = byte2;
            response.data[4]   = static_cast<uint8_t>(fake_raw >> 8);
            response.data[5]   = static_cast<uint8_t>(fake_raw & 0xFF);
            response.data[6]   = 0;
            response.data[7]   = 0;

            head_ = (head_ + 1) % QUEUE_SIZE;
            count_++;
            return true;
        }

        bool receive(CanFrame &out) override
        {
            if (count_ == 0)
            {
                return false;
            }

            out   = queue_[tail_];
            tail_ = (tail_ + 1) % QUEUE_SIZE;
            count_--;
            return true;
        }

        /** The raw value the most recent send() fabricated a response
         * with. Exists for tests: fabricated values include a slow drift
         * (see send()), so a test can no longer recompute the expected
         * value from a fixed formula and must assert against what was
         * actually sent. */
        uint16_t last_fake_raw() const
        {
            return last_fake_raw_;
        }

    private:
        static constexpr uint8_t UDS_REQUEST_SID  = 0x22;
        static constexpr uint8_t UDS_RESPONSE_SID = 0x62;
        static constexpr uint8_t TWO_BYTE_PAYLOAD = 0x05;
        static constexpr uint8_t QUEUE_SIZE       = 8;

        CanFrame queue_[QUEUE_SIZE] = {};
        uint8_t  head_              = 0;
        uint8_t  tail_              = 0;
        uint8_t  count_             = 0;

        /* Monotonic response counter driving the slow value drift in
         * send() -- see there for why fabricated values can't be
         * constant. */
        uint32_t tick_          = 0;
        uint16_t last_fake_raw_ = 0;
        uint32_t rx_id_         = 0;
        uint32_t sends_per_step_;
        int32_t  amplitude_;
};
