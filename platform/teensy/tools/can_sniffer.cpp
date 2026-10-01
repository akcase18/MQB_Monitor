/**
 * @file can_sniffer.cpp
 * @brief Standalone Teensy 4.0 firmware that turns the board into a passive
 * CAN-to-USB sniffer for the Mac monitor tool (tools/monitor). NOT part of
 * the display firmware -- it is its own PlatformIO environment
 * (env:can_sniffer in platformio.ini) and shares nothing with src/main.cpp
 * beyond the FlexCAN_T4 library.
 *
 * Wire protocol: SLCAN (LAWICEL ASCII), the de-facto serial-CAN standard, so
 * the host can drive it with python-can's `slcan` backend instead of a
 * bespoke parser. The command set and frame encoding live in core/can's
 * slcan.hpp, shared with the dash's own sniffer mode (src/main.cpp) so the
 * two answer the host identically; this file is just the bus and USB glue.
 * Received bus frames are streamed out as `t`/`T` lines as they arrive.
 *
 * SAFETY: the Mac tool opens with `L` (listen-only) for passive monitoring
 * between the OBDEleven and the car -- the Teensy is a third node on that bus
 * and must not ACK or otherwise perturb the exchange. `O` (normal) exists
 * only for the later active-probing phase, where the tool deliberately sends
 * its own UDS requests.
 *
 * Hardware: CAN1 peripheral -> pins 22 (CTX1) / 23 (CRX1) on this standalone
 * Teensy 4.0, into its on-board TI CAN transceiver, tapped across CAN-H /
 * CAN-L of the bus between the OBDEleven and the car. This is a DIFFERENT
 * peripheral than the display firmware's TeensyCanBus, which runs on a
 * separate Teensy MicroMod board and uses CAN3 (platform/teensy/src/
 * teensy_can.cpp) -- the two boards just happen to share the same FlexCAN_T4
 * library, not a bus or a peripheral.
 */
#include <Arduino.h>
#include <FlexCAN_T4.h>

#include "slcan.hpp"

namespace
{
    /* FIFO + a deep software RX ring: the MQB bus is busy and USB drain can
     * briefly stall, so over-provision to avoid dropping frames. */
    FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> g_can;

    class SnifferPort : public core::slcan::IPort
    {
        public:
            bool open(uint32_t baud, bool listen_only) override
            {
                g_can.begin();
                g_can.setBaudRate(baud, listen_only ? LISTEN_ONLY : TX);
                g_can.setMBFilter(ACCEPT_ALL);
                g_can.enableFIFO();
                g_can.setFIFOFilter(ACCEPT_ALL);
                open_ = true;
                return true;
            }

            /* FlexCAN_T4 has no explicit end(); re-begin on next open
             * re-inits. Marking closed stops us draining/emitting frames. */
            void close() override
            {
                open_ = false;
            }

            void transmit(const SniffFrame &frame) override
            {
                CAN_message_t msg;
                msg.id             = frame.id;
                msg.len            = frame.len;
                msg.flags.extended = (frame.flags & SNIFF_FLAG_EXTENDED) != 0;
                memcpy(msg.buf, frame.data, frame.len);
                g_can.write(msg);
            }

            void write(const char *data, size_t len) override
            {
                Serial.write(reinterpret_cast<const uint8_t *>(data), len);
            }

            bool is_open() const
            {
                return open_;
            }

        private:
            bool open_ = false;
    };

    SnifferPort          g_port;
    core::slcan::Session g_session(g_port);

    char   g_cmd[core::slcan::COMMAND_MAX + 1];
    size_t g_cmd_len = 0;

    void poll_serial()
    {
        while (Serial.available() > 0)
        {
            char c = static_cast<char>(Serial.read());
            if (c == core::slcan::CR)
            {
                g_session.handle(g_cmd, g_cmd_len);
                g_cmd_len = 0;
            }
            else if (c == '\n' || c == '\0')
            {
                /* ignore */
            }
            else if (g_cmd_len < sizeof(g_cmd) - 1)
            {
                g_cmd[g_cmd_len++] = c;
            }
            else
            {
                /* overflow -- reset the line and error */
                g_cmd_len = 0;
                g_port.write(&core::slcan::BELL, 1);
            }
        }
    }

    void poll_can()
    {
        if (!g_port.is_open())
        {
            return;
        }

        CAN_message_t msg;
        /* Drain everything queued this iteration so a burst doesn't back up
         * into an RX overrun. */
        while (g_can.read(msg))
        {
            SniffFrame frame;
            frame.id    = msg.id;
            frame.ts_ms = static_cast<uint16_t>(millis() % 60000);
            frame.len   = msg.len > 8 ? 8 : msg.len;
            frame.flags = (msg.flags.extended ? SNIFF_FLAG_EXTENDED : 0)
                          | (msg.flags.remote ? SNIFF_FLAG_REMOTE : 0);
            memcpy(frame.data, msg.buf, sizeof(frame.data));

            char   line[core::slcan::ENCODED_MAX];
            size_t n = core::slcan::encode(frame, g_session.timestamps(), line);
            Serial.write(reinterpret_cast<const uint8_t *>(line), n);
        }
    }
} // namespace

void setup()
{
    Serial.begin(115200); /* baud is irrelevant for Teensy USB CDC */
}

void loop()
{
    poll_serial();
    poll_can();
}
