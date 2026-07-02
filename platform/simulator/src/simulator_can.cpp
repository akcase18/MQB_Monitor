/**
 * @file simulator_can.cpp
 * @brief Synthetic ICanBus: any UDS enhanced-data-service request placed
 * via send() gets an immediate fabricated response queued for the next
 * receive(). Fake values are deterministic (derived from byte1/byte2)
 * rather than random, so decode/store round trips are reproducible.
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

    uint8_t  byte1 = frame.data[2];
    uint8_t  byte2 = frame.data[3];
    uint16_t fake_raw
        = static_cast<uint16_t>((byte1 * 173u + byte2 * 37u) % 4096u);

    CanFrame &response = queue_[head_];
    response.id        = rx_id_;
    response.len        = 8;
    response.data[0]    = TWO_BYTE_PAYLOAD;
    response.data[1]    = UDS_RESPONSE_SID;
    response.data[2]    = byte1;
    response.data[3]    = byte2;
    response.data[4]    = static_cast<uint8_t>(fake_raw >> 8);
    response.data[5]    = static_cast<uint8_t>(fake_raw & 0xFF);
    response.data[6]    = 0;
    response.data[7]    = 0;

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
