/**
 * @file teensy_can.cpp
 * @brief FlexCAN_T4 wiring behind ICanBus. CAN3, 16-deep RX/TX ring buffers
 * match the old can_handler.cpp setup. Response filtering by CAN ID is left
 * to the caller (core/can in a later phase) — this layer only moves frames.
 */

#include "teensy_can.hpp"

#include <string.h>

#include <FlexCAN_T4.h>

namespace
{
    FlexCAN_T4<CAN3, RX_SIZE_16, TX_SIZE_16> s_bus;
}

TeensyCanBus &TeensyCanBus::instance()
{
    static TeensyCanBus s_instance;
    return s_instance;
}

bool TeensyCanBus::begin(uint32_t tx_id, uint32_t rx_id, uint32_t baud)
{
    (void)tx_id;

    s_bus.begin();
    s_bus.setBaudRate(baud);
    s_bus.setMBFilter(REJECT_ALL);

    /* Four mailboxes on the response id, not one. FlexCAN mailboxes do
     * not chain: a second matching frame arriving before the CPU has read
     * the first overwrites it, so a single open mailbox could only ever
     * hand up one frame per receive() cycle no matter how many arrived.
     * core::SignalPoller keeps one request outstanding at a time now, but
     * a single request can still answer in more than one frame -- an 0x78
     * "response pending" NRC followed by the real reading is the ordinary
     * case -- and read() walks the mailboxes in order, so the spares cost
     * nothing but stop a burst collapsing into its last frame.
     *
     * setMBFilterRange(mb, id, id) rather than the obvious
     * setMBFilter(mb, id): the two compute an identical mask and an
     * identical exact-match software filter, but setMBFilter is FLASHMEM
     * in the vendored library, and with four call sites the compiler
     * stops inlining it and emits an out-of-line copy whose section
     * collides with setBaudRate's ("causes a section type conflict ... in
     * section '.flashmem'"). setMBFilterRange carries no such attribute.
     * The REJECT_ALL call above is a different overload and still
     * inlines. */
    s_bus.setMBFilterRange(MB1, rx_id, rx_id);
    s_bus.setMBFilterRange(MB2, rx_id, rx_id);
    s_bus.setMBFilterRange(MB3, rx_id, rx_id);
    s_bus.setMBFilterRange(MB4, rx_id, rx_id);
    return true;
}

namespace
{
    /* Both payload arrays are 8 bytes (CanFrame::data, CAN_message_t::buf),
     * so this only ever clamps a frame claiming a length longer than a
     * classic CAN frame can hold. */
    uint8_t clamped_len(uint8_t len)
    {
        return len > 8 ? 8 : len;
    }
} // namespace

bool TeensyCanBus::send(const CanFrame &frame)
{
    CAN_message_t msg;
    msg.id  = frame.id;
    msg.len = clamped_len(frame.len);
    memcpy(msg.buf, frame.data, msg.len);

    return s_bus.write(msg) > 0;
}

bool TeensyCanBus::receive(CanFrame &out)
{
    CAN_message_t msg;
    if (s_bus.read(msg) <= 0)
    {
        return false;
    }

    out.id  = msg.id;
    out.len = clamped_len(msg.len);
    memcpy(out.data, msg.buf, out.len);

    return true;
}
