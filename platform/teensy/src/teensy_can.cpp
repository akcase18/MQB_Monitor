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

bool TeensyCanBus::begin(uint32_t rx_id_lo, uint32_t rx_id_hi, uint32_t baud)
{
    rx_id_lo_ = rx_id_lo;
    rx_id_hi_ = rx_id_hi;
    baud_     = baud;

    s_bus.begin();
    s_bus.setBaudRate(baud);
    s_bus.setMBFilter(REJECT_ALL);

    /**
     * Four mailboxes on the response ids, not one. FlexCAN mailboxes do
     * not chain: a second matching frame arriving before the CPU has read
     * the first overwrites it, so a single open mailbox could only ever
     * hand up one frame per receive() cycle no matter how many arrived.
     * core::SignalPoller keeps one request outstanding at a time now, but
     * a single request can still answer in more than one frame -- an 0x78
     * "response pending" NRC followed by the real reading is the ordinary
     * case -- and read() walks the mailboxes in order, so the spares cost
     * nothing but stop a burst collapsing into its last frame.
     *
     * setMBFilterRange() covers every module's response id; the hardware
     * mask passes everything sharing the range's top bits and core/can
     * filters exactly. It refuses (returns false, leaving the mailbox
     * rejecting everything) a range wider than 1000 ids, which
     * tools/gen_catalog.py won't generate. It was the call here even back
     * when only one id was received, rather than the obvious
     * setMBFilter(mb, id): the two compute an identical mask and an
     * identical exact-match software filter, but setMBFilter is FLASHMEM
     * in the vendored library, and with four call sites the compiler
     * stops inlining it and emits an out-of-line copy whose section
     * collides with setBaudRate's ("causes a section type conflict ... in
     * section '.flashmem'"). setMBFilterRange carries no such attribute.
     * The REJECT_ALL call above is a different overload and still
     * inlines.
     */
    s_bus.setMBFilterRange(MB1, rx_id_lo, rx_id_hi);
    s_bus.setMBFilterRange(MB2, rx_id_lo, rx_id_hi);
    s_bus.setMBFilterRange(MB3, rx_id_lo, rx_id_hi);
    s_bus.setMBFilterRange(MB4, rx_id_lo, rx_id_hi);
    return true;
}

namespace
{
    /**
     * Both payload arrays are 8 bytes (CanFrame::data, CAN_message_t::buf),
     * so this only ever clamps a frame claiming a length longer than a
     * classic CAN frame can hold.
     */
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

/*==============================================================================
 * Sniffer mode
 *
 * s_bus's own RX ring stays 16 deep -- it lives inside a static object in
 * RAM1, and growing it would cost every boot whether sniffing or not.
 * Sniffing instead takes frames straight from the FIFO interrupt into a
 * ring of its own in RAM2 (DMAMEM), which is nearly empty: the 6-deep
 * hardware FIFO alone lasts ~1.5 ms on a busy 500k bus, less than one LVGL
 * pass or a USB hiccup, so loop()-paced polling would drop frames.
 *
 * Single producer (the ISR), single consumer (loop() via sniff_read()), so
 * the two indices need only be volatile -- each is written by one side.
 =============================================================================*/

namespace
{
    constexpr uint32_t SNIFF_RING_SIZE = 512; /* power of two; 8 KB */

    DMAMEM SniffFrame s_ring[SNIFF_RING_SIZE];

    volatile uint32_t s_head    = 0; /* ISR writes */
    volatile uint32_t s_tail    = 0; /* loop() writes */
    volatile uint32_t s_rx      = 0;
    volatile uint32_t s_dropped = 0;

    /**
     * FLASHMEM by another name. Plain FLASHMEM here collides with the
     * template's own FLASHMEM members (setBaudRate et al.), which GCC emits
     * as COMDAT into the same ".flashmem" section -- the same "section type
     * conflict" begin()'s setMBFilterRange note above dodges. The linker
     * script collects *(.flashmem*), so a subsection lands in flash all
     * the same.
     */
    #define SNIFF_FLASHMEM __attribute__((section(".flashmem.sniff")))

    /**
     * Called from the CAN3 ISR -- FlexCAN_T4 dispatches onReceive handlers
     * directly from flexcan_interrupt() as long as events() is never
     * called, which nothing in this firmware does.
     */
    void sniff_isr(const CAN_message_t &msg)
    {
        s_rx = s_rx + 1;

        uint32_t head = s_head;
        if (head - s_tail >= SNIFF_RING_SIZE)
        {
            s_dropped = s_dropped + 1;
            return;
        }

        SniffFrame &f = s_ring[head & (SNIFF_RING_SIZE - 1)];
        f.id          = msg.id;
        f.ts_ms       = static_cast<uint16_t>(millis() % 60000);
        f.len         = clamped_len(msg.len);
        f.flags       = (msg.flags.extended ? SNIFF_FLAG_EXTENDED : 0)
                  | (msg.flags.remote ? SNIFF_FLAG_REMOTE : 0);
        memcpy(f.data, msg.buf, sizeof(f.data));

        s_head = head + 1;
    }
} // namespace

SNIFF_FLASHMEM bool TeensyCanBus::sniff_begin(uint32_t baud, bool listen_only)
{
    s_head    = 0;
    s_tail    = 0;
    s_rx      = 0;
    s_dropped = 0;

    s_bus.begin();
    s_bus.setBaudRate(baud, listen_only ? LISTEN_ONLY : TX);
    s_bus.setMBFilter(ACCEPT_ALL);
    s_bus.enableFIFO();
    s_bus.setFIFOFilter(ACCEPT_ALL);
    s_bus.onReceive(sniff_isr);
    s_bus.enableFIFOInterrupt();

    sniffing_    = true;
    listen_only_ = listen_only;
    return true;
}

SNIFF_FLASHMEM void TeensyCanBus::sniff_end()
{
    if (!sniffing_)
    {
        return;
    }

    s_bus.disableFIFOInterrupt();
    s_bus.onReceive(nullptr);
    sniffing_ = false;

    /**
     * begin() soft-resets the controller and drops the FIFO, so this is
     * the exact boot-time REJECT_ALL + response-id mailbox setup again.
     */
    begin(rx_id_lo_, rx_id_hi_, baud_);
}

bool TeensyCanBus::sniff_read(SniffFrame &out)
{
    uint32_t tail = s_tail;
    if (tail == s_head)
    {
        return false;
    }

    out    = s_ring[tail & (SNIFF_RING_SIZE - 1)];
    s_tail = tail + 1;
    return true;
}

SNIFF_FLASHMEM bool TeensyCanBus::sniff_send(const SniffFrame &frame)
{
    if (!sniffing_ || listen_only_)
    {
        return false;
    }

    CAN_message_t msg;
    msg.id             = frame.id;
    msg.len            = clamped_len(frame.len);
    msg.flags.extended = (frame.flags & SNIFF_FLAG_EXTENDED) != 0;
    memcpy(msg.buf, frame.data, msg.len);

    return s_bus.write(msg) > 0;
}

SniffStats TeensyCanBus::sniff_stats() const
{
    return SniffStats{s_rx, s_dropped};
}

/*==============================================================================
 * Bench mode
 *
 * The v4 dash as THROUGHPUT_TEST_PLAN.md's instrument. Every frame comes
 * off the FIFO interrupt straight into the sink (core::bench::BenchEngine,
 * via src/main.cpp), stamped with micros() there and then, and a PIT timer
 * ticks the sink every BENCH_TICK_US. Both interrupts run at the same NVIC
 * priority, so neither preempts the other and the engine needs no locks
 * between them. The timer is PIT channel 0 programmed directly, not
 * IntervalTimer, whose code would land in ITCM; nothing else in this
 * firmware uses the PIT.
 *
 * Transmits go through FlexCAN_T4's sequential path (`seq = 1`): one
 * mailbox, the rest queued in order and fed from that mailbox's
 * transmit-complete interrupt. With several mailboxes free, two frames
 * with the same id could leave in either order, and ISO-TP consecutive
 * frames can't tolerate that.
 *
 * All of it lives in flash: RAM1 code sits a few hundred bytes under a
 * 32 KB ITCM block boundary, and this only ever runs with the dashboard
 * paused.
 =============================================================================*/

namespace
{
    #define BENCH_FLASHMEM __attribute__((section(".flashmem.bench")))

    constexpr uint8_t BENCH_IRQ_PRIORITY = 128;

    /**
     * The PIT counts the 24 MHz peripheral clock.
     */
    constexpr uint32_t PIT_CYCLES_PER_US = 24;

    IBenchSink *s_bench_sink = nullptr;

    BENCH_FLASHMEM void bench_isr(const CAN_message_t &msg)
    {
        uint32_t t_us = micros();
        if (s_bench_sink == nullptr)
        {
            return;
        }

        CanFrame frame;
        frame.id  = msg.id;
        frame.len = clamped_len(msg.len);
        memcpy(frame.data, msg.buf, sizeof(frame.data));
        s_bench_sink->on_bench_frame(frame, t_us);
    }

    BENCH_FLASHMEM void bench_tick_isr()
    {
        PIT_TFLG0 = 1;
        asm volatile("dsb"); /* clear the flag before returning, or it re-fires */

        if (s_bench_sink != nullptr)
        {
            s_bench_sink->on_bench_tick(micros());
        }
    }

    BENCH_FLASHMEM void bench_timer_start()
    {
        CCM_CCGR1 |= CCM_CCGR1_PIT(CCM_CCGR_ON);
        PIT_MCR    = 0;
        PIT_TCTRL0 = 0;
        PIT_TFLG0  = 1;
        PIT_LDVAL0 = BENCH_TICK_US * PIT_CYCLES_PER_US - 1;
        attachInterruptVector(IRQ_PIT, bench_tick_isr);
        NVIC_SET_PRIORITY(IRQ_PIT, BENCH_IRQ_PRIORITY);
        NVIC_ENABLE_IRQ(IRQ_PIT);
        PIT_TCTRL0 = PIT_TCTRL_TIE | PIT_TCTRL_TEN;
    }

    BENCH_FLASHMEM void bench_timer_stop()
    {
        PIT_TCTRL0 = 0;
        PIT_TFLG0  = 1;
        NVIC_DISABLE_IRQ(IRQ_PIT);
    }
} // namespace

BENCH_FLASHMEM bool TeensyCanBus::bench_begin(uint32_t    baud,
                                              bool        listen_only,
                                              IBenchSink *sink)
{
    if (sniffing_ || benching_ || sink == nullptr)
    {
        return false;
    }

    s_bench_sink = sink;

    s_bus.begin();
    s_bus.setBaudRate(baud, listen_only ? LISTEN_ONLY : TX);
    s_bus.setMBFilter(REJECT_ALL);
    s_bus.enableFIFO();
    s_bus.setFIFOFilter(ACCEPT_ALL);
    s_bus.onReceive(bench_isr);
    s_bus.enableFIFOInterrupt();

    /**
     * Mailbox interrupts on, straight into the mask registers: MB0-7 are
     * the FIFO and its filter table, and the receive mailboxes past them
     * reject everything, so in practice this is the transmit mailboxes'
     * completion interrupt -- what drains the sequential transmit queue.
     * Not enableMBInterrupts(): it calls a FLASHMEM template member that
     * doesn't inline here, which lands in the same "section type
     * conflict" begin()'s setMBFilterRange note describes.
     */
    FLEXCANb_IMASK1(CAN3) |= 0xFFFFFF00u;
    FLEXCANb_IMASK2(CAN3) = 0xFFFFFFFFu;

    NVIC_SET_PRIORITY(IRQ_CAN3, BENCH_IRQ_PRIORITY);
    bench_timer_start();

    benching_    = true;
    listen_only_ = listen_only;
    return true;
}

BENCH_FLASHMEM void TeensyCanBus::bench_end()
{
    if (!benching_)
    {
        return;
    }

    bench_timer_stop();
    FLEXCANb_IMASK1(CAN3) = 0;
    FLEXCANb_IMASK2(CAN3) = 0;
    s_bus.onReceive(nullptr);
    s_bench_sink = nullptr;
    benching_    = false;

    begin(rx_id_lo_, rx_id_hi_, baud_);
}

BENCH_FLASHMEM bool TeensyCanBus::bench_send(const CanFrame &frame)
{
    if (!benching_ || listen_only_)
    {
        return false;
    }

    CAN_message_t msg;
    msg.id             = frame.id;
    msg.len            = clamped_len(frame.len);
    msg.flags.extended = frame.id > 0x7FF;
    msg.seq            = 1;
    memcpy(msg.buf, frame.data, msg.len);

    return s_bus.write(msg) != 0;
}

BENCH_FLASHMEM uint32_t TeensyCanBus::bench_micros() const
{
    return micros();
}
