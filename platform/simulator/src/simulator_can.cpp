/**
 * @file simulator_can.cpp
 * @brief Synthetic ICanBus: any UDS enhanced-data-service request placed
 * via send() gets an immediate fabricated response queued for the next
 * receive(). Fake values are deterministic -- a per-signal base derived
 * from byte1/byte2 plus a slow triangular drift off a monotonic response
 * counter, never a RNG -- so a given run still reproduces exactly.
 */
#include "simulator_can.hpp"

namespace
{
    constexpr uint8_t UDS_REQUEST_SID  = 0x22;
    constexpr uint8_t UDS_RESPONSE_SID = 0x62;
    constexpr uint8_t TWO_BYTE_PAYLOAD = 0x05;
} // namespace

SimulatorCanBus &SimulatorCanBus::instance()
{
    static SimulatorCanBus s_instance;
    return s_instance;
}

bool SimulatorCanBus::begin(uint32_t tx_id, uint32_t rx_id, uint32_t baud)
{
    (void)tx_id;
    (void)baud;
    rx_id_ = rx_id;
    return true;
}

bool SimulatorCanBus::send(const CanFrame &frame)
{
    if (frame.len < 4 || frame.data[1] != UDS_REQUEST_SID
        || count_ >= QUEUE_SIZE)
    {
        return false;
    }

    uint8_t byte1 = frame.data[2];
    uint8_t byte2 = frame.data[3];

    /**
     * A per-signal base, plus a slow triangular sweep so values actually
     * move over time. The sweep is not cosmetic: with a constant response
     * per signal, core::Logger's on-change recording correctly writes
     * nothing at all, which left the entire logging path impossible to
     * exercise in the simulator (and every panel reading frozen). The
     * phase is offset per signal so they don't sweep in lockstep. */
    int32_t base = static_cast<int32_t>((byte1 * 173u + byte2 * 37u) % 4096u);

    uint32_t phase = ((tick_++ / 8u) + byte1 + byte2) % 512u;
    int32_t  ramp  = phase < 256u ? static_cast<int32_t>(phase)
                                  : static_cast<int32_t>(512u - phase);
    int32_t  drift = (ramp - 128) * 2; /* -256 .. +256 */

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

bool SimulatorCanBus::receive(CanFrame &out)
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
