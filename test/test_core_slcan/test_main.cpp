/**
 * @file test_main.cpp
 * @brief Host-buildable coverage for core/can's SLCAN codec: frame encoding
 * (standard/extended ids, DLC clamping, the timestamp suffix) and Session's
 * command handling against a fake IPort -- the open/close handshake, the
 * closed-only bitrate rule, listen-only swallowing transmits, and BELL on
 * anything malformed. Run with `pio test -e simulator -f test_core_slcan`.
 */
#include <string.h>
#include <unity.h>

#include "slcan.hpp"

namespace
{
    using namespace core::slcan;

    class FakePort : public IPort
    {
        public:
            bool open(uint32_t baud, bool listen_only) override
            {
                opens++;
                last_baud   = baud;
                last_listen = listen_only;
                return open_result;
            }

            void close() override
            {
                closes++;
            }

            void transmit(const SniffFrame &frame) override
            {
                transmits++;
                last_tx = frame;
            }

            void write(const char *data, size_t len) override
            {
                memcpy(out + out_len, data, len);
                out_len += len;
                out[out_len] = '\0';
            }

            void clear_output()
            {
                out_len = 0;
                out[0]  = '\0';
            }

            bool       open_result = true;
            int        opens       = 0;
            int        closes      = 0;
            int        transmits   = 0;
            uint32_t   last_baud   = 0;
            bool       last_listen = false;
            SniffFrame last_tx {};

            char   out[256] = "";
            size_t out_len  = 0;
    };

    void send(Session &session, const char *line)
    {
        session.handle(line, strlen(line));
    }

    SniffFrame frame(uint32_t id, uint8_t len, bool ext = false)
    {
        SniffFrame f {};
        f.id    = id;
        f.len   = len;
        f.flags = ext ? SNIFF_FLAG_EXTENDED : 0;
        for (uint8_t i = 0; i < 8; i++)
        {
            f.data[i] = static_cast<uint8_t>(0x10 * i + i);
        }
        return f;
    }

    void test_encode_standard()
    {
        char   out[ENCODED_MAX];
        size_t n = encode(frame(0x7E8, 3), false, out);
        TEST_ASSERT_EQUAL(12, n);
        TEST_ASSERT_EQUAL_MEMORY("t7E83001122\r", out, n);
    }

    void test_encode_extended()
    {
        char   out[ENCODED_MAX];
        size_t n = encode(frame(0x18DAF110, 1, true), false, out);
        TEST_ASSERT_EQUAL_MEMORY("T18DAF110100\r", out, n);
    }

    void test_encode_clamps_dlc_and_fits_longest()
    {
        SniffFrame f = frame(0x1FFFFFFF, 15, true);
        f.ts_ms      = 0xBEEF;

        char   out[ENCODED_MAX];
        size_t n = encode(f, true, out);

        /**
         * 'T' + 8 id + dlc + 16 data + 4 ts + CR
         */
        TEST_ASSERT_EQUAL(31, n);
        TEST_ASSERT_TRUE(n <= ENCODED_MAX);
        TEST_ASSERT_EQUAL_MEMORY("T1FFFFFFF8", out, 10);
        TEST_ASSERT_EQUAL_MEMORY("BEEF\r", out + n - 5, 5);
    }

    void test_handshake_listen_only()
    {
        FakePort port;
        Session  session(port);

        send(session, "S6");
        send(session, "L");

        TEST_ASSERT_EQUAL_STRING("\r\r", port.out);
        TEST_ASSERT_EQUAL(1, port.opens);
        TEST_ASSERT_EQUAL_UINT32(500000, port.last_baud);
        TEST_ASSERT_TRUE(port.last_listen);
        TEST_ASSERT_TRUE(session.is_open());
        TEST_ASSERT_TRUE(session.is_listen_only());

        send(session, "C");
        TEST_ASSERT_EQUAL(1, port.closes);
        TEST_ASSERT_FALSE(session.is_open());
    }

    void test_close_when_closed_and_double_open_ring_bell()
    {
        FakePort port;
        Session  session(port);

        send(session, "C");
        TEST_ASSERT_EQUAL_STRING("\a", port.out);

        port.clear_output();
        send(session, "O");
        send(session, "L");
        TEST_ASSERT_EQUAL_STRING("\r\a", port.out);
        TEST_ASSERT_EQUAL(1, port.opens);
    }

    void test_bitrate_rejected_while_open_or_unknown()
    {
        FakePort port;
        Session  session(port);

        send(session, "S9");
        send(session, "S");
        TEST_ASSERT_EQUAL_STRING("\a\a", port.out);

        port.clear_output();
        send(session, "S4");
        send(session, "O");
        send(session, "S6");
        TEST_ASSERT_EQUAL_STRING("\r\r\a", port.out);
        TEST_ASSERT_EQUAL_UINT32(125000, port.last_baud);
    }

    void test_failed_open_stays_closed()
    {
        FakePort port;
        port.open_result = false;
        Session session(port);

        send(session, "L");
        TEST_ASSERT_EQUAL_STRING("\a", port.out);
        TEST_ASSERT_FALSE(session.is_open());
    }

    void test_transmit_normal_mode()
    {
        FakePort port;
        Session  session(port);

        send(session, "O");
        port.clear_output();
        send(session, "t7E0322F190");

        TEST_ASSERT_EQUAL_STRING("\r", port.out);
        TEST_ASSERT_EQUAL(1, port.transmits);
        TEST_ASSERT_EQUAL_HEX32(0x7E0, port.last_tx.id);
        TEST_ASSERT_EQUAL(3, port.last_tx.len);
        TEST_ASSERT_EQUAL_HEX8(0x22, port.last_tx.data[0]);
        TEST_ASSERT_EQUAL_HEX8(0x90, port.last_tx.data[2]);
        TEST_ASSERT_EQUAL(0, port.last_tx.flags);
    }

    void test_transmit_extended()
    {
        FakePort port;
        Session  session(port);

        send(session, "O");
        send(session, "T18DAF1101AB");

        TEST_ASSERT_EQUAL(1, port.transmits);
        TEST_ASSERT_EQUAL_HEX32(0x18DAF110, port.last_tx.id);
        TEST_ASSERT_EQUAL(SNIFF_FLAG_EXTENDED, port.last_tx.flags);
        TEST_ASSERT_EQUAL_HEX8(0xAB, port.last_tx.data[0]);
    }

    /**
     * Acked but never put on the bus -- python-can's keep-alives must not
     * fault a silent tap. Same when the channel isn't open at all.
     */
    void test_transmit_swallowed_when_listen_only_or_closed()
    {
        FakePort port;
        Session  session(port);

        send(session, "t7E0100");
        send(session, "L");
        send(session, "t7E0100");

        TEST_ASSERT_EQUAL_STRING("\r\r\r", port.out);
        TEST_ASSERT_EQUAL(0, port.transmits);
    }

    void test_malformed_transmit_rings_bell()
    {
        FakePort port;
        Session  session(port);
        send(session, "O");
        port.clear_output();

        send(session, "t7E");         /* no dlc */
        send(session, "t7EG100");     /* bad id hex */
        send(session, "t7E09");       /* dlc > 8 */
        send(session, "t7E03AABB");   /* short payload */
        send(session, "t7E01ZZ");     /* bad payload hex */

        TEST_ASSERT_EQUAL_STRING("\a\a\a\a\a", port.out);
        TEST_ASSERT_EQUAL(0, port.transmits);
    }

    void test_queries_and_timestamps()
    {
        FakePort port;
        Session  session(port);

        send(session, "V");
        send(session, "N");
        send(session, "F");
        TEST_ASSERT_EQUAL_STRING("V0100\rNMQB1\rF00\r", port.out);

        TEST_ASSERT_FALSE(session.timestamps());
        send(session, "Z1");
        TEST_ASSERT_TRUE(session.timestamps());
        send(session, "Z0");
        TEST_ASSERT_FALSE(session.timestamps());

        port.clear_output();
        send(session, "Z");
        send(session, "X");
        session.handle("", 0);
        TEST_ASSERT_EQUAL_STRING("\a\a\r", port.out);
    }

    void test_reset_closes_and_restores_defaults()
    {
        FakePort port;
        Session  session(port);

        send(session, "S4");
        send(session, "Z1");
        send(session, "O");
        session.reset();

        TEST_ASSERT_EQUAL(1, port.closes);
        TEST_ASSERT_FALSE(session.is_open());
        TEST_ASSERT_FALSE(session.timestamps());

        /**
         * Back at the 500k default, and a reset while closed doesn't
         * close again.
         */
        session.reset();
        TEST_ASSERT_EQUAL(1, port.closes);
        send(session, "L");
        TEST_ASSERT_EQUAL_UINT32(500000, port.last_baud);
    }
} // namespace

void setUp() {}
void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_standard);
    RUN_TEST(test_encode_extended);
    RUN_TEST(test_encode_clamps_dlc_and_fits_longest);
    RUN_TEST(test_handshake_listen_only);
    RUN_TEST(test_close_when_closed_and_double_open_ring_bell);
    RUN_TEST(test_bitrate_rejected_while_open_or_unknown);
    RUN_TEST(test_failed_open_stays_closed);
    RUN_TEST(test_transmit_normal_mode);
    RUN_TEST(test_transmit_extended);
    RUN_TEST(test_transmit_swallowed_when_listen_only_or_closed);
    RUN_TEST(test_malformed_transmit_rings_bell);
    RUN_TEST(test_queries_and_timestamps);
    RUN_TEST(test_reset_closes_and_restores_defaults);
    return UNITY_END();
}
