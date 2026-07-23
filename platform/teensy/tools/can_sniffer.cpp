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
 * bespoke parser. A useful subset is implemented -- enough for python-can's
 * open/close handshake plus optional host->bus transmit for the (future)
 * active-probing phase:
 *
 *   Sn        set bitrate by index (S6 = 500k, the MQB rate); closed only
 *   O         open the channel, NORMAL mode (can ACK + transmit)
 *   L         open the channel, LISTEN-ONLY (silent; never ACKs/transmits)
 *   C         close the channel
 *   t/T       transmit an 11-bit / 29-bit data frame (ignored in listen-only)
 *   Z0 / Z1   disable / enable the 4-hex-digit millisecond timestamp suffix
 *   V / N     firmware version / serial number query
 *   F         status flags (returns F00 -- no errors tracked)
 *
 * Each accepted command replies CR (0x0D); a rejected one replies BELL
 * (0x07), per the SLCAN convention. Received bus frames are streamed out as
 * `t`/`T` lines as they arrive.
 *
 * SAFETY: the Mac tool opens with `L` (listen-only) for passive monitoring
 * between the OBDEleven and the car -- the Teensy is a third node on that bus
 * and must not ACK or otherwise perturb the exchange. `O` (normal) exists
 * only for the later active-probing phase, where the tool deliberately sends
 * its own UDS requests.
 *
 * Hardware: CAN3 peripheral -> pins 30 (CRX3) / 31 (CTX3) on the Teensy 4.0,
 * into the on-board TI CAN transceiver, tapped across CAN-H / CAN-L of the
 * bus between the OBDEleven and the car. Same CAN3 peripheral the display
 * firmware's TeensyCanBus uses (platform/teensy/src/teensy_can.cpp).
 */
#include <Arduino.h>
#include <FlexCAN_T4.h>

namespace
{
    /* FIFO + a deep software RX ring: the MQB bus is busy and USB drain can
     * briefly stall, so over-provision to avoid dropping frames. */
    FlexCAN_T4<CAN3, RX_SIZE_256, TX_SIZE_16> g_can;

    constexpr char CR   = '\r';
    constexpr char BELL = '\a'; /* 0x07 */

    constexpr uint32_t DEFAULT_BAUD = 500000; /* MQB powertrain CAN */

    bool     g_open        = false;
    bool     g_listen_only = true;
    bool     g_timestamps  = false;
    uint32_t g_baud        = DEFAULT_BAUD;

    /* Line assembly for inbound host commands. SLCAN commands are short; a
     * transmit command tops out well under 32 chars. */
    char   g_cmd[40];
    size_t g_cmd_len = 0;

    char hex_digit(uint8_t nibble)
    {
        return "0123456789ABCDEF"[nibble & 0x0F];
    }

    /* Parses `count` hex chars from `s` into a value. Returns false on any
     * non-hex char. */
    bool parse_hex(const char *s, uint8_t count, uint32_t &out)
    {
        uint32_t v = 0;
        for (uint8_t i = 0; i < count; i++)
        {
            char     c = s[i];
            uint8_t  d;
            if (c >= '0' && c <= '9')
                d = c - '0';
            else if (c >= 'A' && c <= 'F')
                d = c - 'A' + 10;
            else if (c >= 'a' && c <= 'f')
                d = c - 'a' + 10;
            else
                return false;
            v = (v << 4) | d;
        }
        out = v;
        return true;
    }

    void reply_ok()
    {
        Serial.write(CR);
    }

    void reply_err()
    {
        Serial.write(BELL);
    }

    /* Maps an SLCAN Sn bitrate index to a bit/s value. Returns 0 for an
     * index we don't support. */
    uint32_t baud_for_index(char idx)
    {
        switch (idx)
        {
            case '0': return 10000;
            case '1': return 20000;
            case '2': return 50000;
            case '3': return 100000;
            case '4': return 125000;
            case '5': return 250000;
            case '6': return 500000;
            case '7': return 800000;
            case '8': return 1000000;
            default:  return 0;
        }
    }

    void open_channel(bool listen_only)
    {
        g_listen_only = listen_only;
        g_can.begin();
        g_can.setBaudRate(g_baud, listen_only ? LISTEN_ONLY : TX);
        g_can.setMBFilter(ACCEPT_ALL);
        g_can.enableFIFO();
        g_can.setFIFOFilter(ACCEPT_ALL);
        g_open = true;
    }

    void close_channel()
    {
        /* FlexCAN_T4 has no explicit end(); re-begin on next open re-inits.
         * Marking closed stops us draining/emitting frames. */
        g_open = false;
    }

    /* Emits one received frame as an SLCAN line into the USB serial stream. */
    void emit_frame(const CAN_message_t &msg)
    {
        char    out[32];
        size_t  n   = 0;
        bool    ext = msg.flags.extended;

        out[n++] = ext ? 'T' : 't';

        if (ext)
        {
            for (int shift = 28; shift >= 0; shift -= 4)
                out[n++] = hex_digit((msg.id >> shift) & 0x0F);
        }
        else
        {
            out[n++] = hex_digit((msg.id >> 8) & 0x0F);
            out[n++] = hex_digit((msg.id >> 4) & 0x0F);
            out[n++] = hex_digit(msg.id & 0x0F);
        }

        uint8_t len = msg.len > 8 ? 8 : msg.len;
        out[n++]    = hex_digit(len);

        for (uint8_t i = 0; i < len; i++)
        {
            out[n++] = hex_digit(msg.buf[i] >> 4);
            out[n++] = hex_digit(msg.buf[i] & 0x0F);
        }

        if (g_timestamps)
        {
            uint16_t ts = static_cast<uint16_t>(millis() % 60000);
            out[n++]    = hex_digit((ts >> 12) & 0x0F);
            out[n++]    = hex_digit((ts >> 8) & 0x0F);
            out[n++]    = hex_digit((ts >> 4) & 0x0F);
            out[n++]    = hex_digit(ts & 0x0F);
        }

        out[n++] = CR;
        Serial.write(reinterpret_cast<const uint8_t *>(out), n);
    }

    /* Handles a host transmit command (t/T). Ignored (but ACKed) in
     * listen-only so python-can's periodic keep-alives can't fault the bus. */
    void handle_transmit(const char *cmd, size_t len, bool ext)
    {
        uint8_t  id_chars = ext ? 8 : 3;
        size_t   min_len  = 1u + id_chars + 1u; /* kind + id + dlc */
        if (len < min_len)
        {
            reply_err();
            return;
        }

        uint32_t id;
        if (!parse_hex(cmd + 1, id_chars, id))
        {
            reply_err();
            return;
        }

        uint32_t dlc;
        if (!parse_hex(cmd + 1 + id_chars, 1, dlc) || dlc > 8)
        {
            reply_err();
            return;
        }

        if (len < min_len + dlc * 2)
        {
            reply_err();
            return;
        }

        CAN_message_t msg;
        msg.id            = id;
        msg.len           = static_cast<uint8_t>(dlc);
        msg.flags.extended = ext;
        const char *data  = cmd + 1 + id_chars + 1;
        for (uint32_t i = 0; i < dlc; i++)
        {
            uint32_t byte;
            if (!parse_hex(data + i * 2, 2, byte))
            {
                reply_err();
                return;
            }
            msg.buf[i] = static_cast<uint8_t>(byte);
        }

        if (g_open && !g_listen_only)
            g_can.write(msg);

        reply_ok();
    }

    void handle_command(const char *cmd, size_t len)
    {
        if (len == 0)
        {
            reply_ok(); /* bare CR -- treat as no-op */
            return;
        }

        switch (cmd[0])
        {
            case 'S': /* set bitrate index */
            {
                if (g_open || len < 2)
                {
                    reply_err();
                    return;
                }
                uint32_t b = baud_for_index(cmd[1]);
                if (b == 0)
                {
                    reply_err();
                    return;
                }
                g_baud = b;
                reply_ok();
                return;
            }
            case 'O': /* open, normal mode */
                if (g_open)
                    reply_err();
                else
                {
                    open_channel(false);
                    reply_ok();
                }
                return;
            case 'L': /* open, listen-only */
                if (g_open)
                    reply_err();
                else
                {
                    open_channel(true);
                    reply_ok();
                }
                return;
            case 'C': /* close */
                if (!g_open)
                    reply_err();
                else
                {
                    close_channel();
                    reply_ok();
                }
                return;
            case 't':
                handle_transmit(cmd, len, false);
                return;
            case 'T':
                handle_transmit(cmd, len, true);
                return;
            case 'Z': /* timestamp on/off */
                if (len < 2)
                    reply_err();
                else
                {
                    g_timestamps = (cmd[1] == '1');
                    reply_ok();
                }
                return;
            case 'F': /* status flags -- none tracked */
                Serial.print("F00");
                Serial.write(CR);
                return;
            case 'V': /* version */
                Serial.print("V0100");
                Serial.write(CR);
                return;
            case 'N': /* serial number */
                Serial.print("NMQB1");
                Serial.write(CR);
                return;
            default:
                reply_err();
                return;
        }
    }

    void poll_serial()
    {
        while (Serial.available() > 0)
        {
            char c = static_cast<char>(Serial.read());
            if (c == CR)
            {
                handle_command(g_cmd, g_cmd_len);
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
                reply_err();
            }
        }
    }

    void poll_can()
    {
        if (!g_open)
            return;

        CAN_message_t msg;
        /* Drain everything queued this iteration so a burst doesn't back up
         * into an RX overrun. */
        while (g_can.read(msg))
            emit_frame(msg);
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
