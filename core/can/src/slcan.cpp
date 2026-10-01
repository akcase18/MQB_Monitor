#include "slcan.hpp"

#include "mem_placement.hpp"

namespace core
{
    namespace slcan
    {
        namespace
        {
            FLASHMEM char hex_digit(uint8_t nibble)
            {
                return "0123456789ABCDEF"[nibble & 0x0F];
            }

            /* Parses `count` hex chars from `s` into a value. Returns false
             * on any non-hex char. */
            FLASHMEM bool parse_hex(const char *s, uint8_t count, uint32_t &out)
            {
                uint32_t v = 0;
                for (uint8_t i = 0; i < count; i++)
                {
                    char    c = s[i];
                    uint8_t d;
                    if (c >= '0' && c <= '9')
                    {
                        d = c - '0';
                    }
                    else if (c >= 'A' && c <= 'F')
                    {
                        d = c - 'A' + 10;
                    }
                    else if (c >= 'a' && c <= 'f')
                    {
                        d = c - 'a' + 10;
                    }
                    else
                    {
                        return false;
                    }
                    v = (v << 4) | d;
                }
                out = v;
                return true;
            }
        } // namespace

        /* Per-frame, but FLASHMEM all the same: on the dash it only runs in
         * sniffer mode, with the dashboard paused, and RAM1 code there sits
         * just under a 32 KB ITCM block boundary (see main.cpp's
         * pump_sniff()). */
        FLASHMEM size_t encode(const SniffFrame &frame, bool timestamps, char *out)
        {
            size_t n   = 0;
            bool   ext = (frame.flags & SNIFF_FLAG_EXTENDED) != 0;

            out[n++] = ext ? 'T' : 't';

            for (int shift = ext ? 28 : 8; shift >= 0; shift -= 4)
            {
                out[n++] = hex_digit(frame.id >> shift);
            }

            uint8_t len = frame.len > 8 ? 8 : frame.len;
            out[n++]    = hex_digit(len);

            for (uint8_t i = 0; i < len; i++)
            {
                out[n++] = hex_digit(frame.data[i] >> 4);
                out[n++] = hex_digit(frame.data[i]);
            }

            if (timestamps)
            {
                for (int shift = 12; shift >= 0; shift -= 4)
                {
                    out[n++] = hex_digit(frame.ts_ms >> shift);
                }
            }

            out[n++] = CR;
            return n;
        }

        FLASHMEM uint32_t baud_for_index(char idx)
        {
            switch (idx)
            {
                case '0':
                    return 10000;
                case '1':
                    return 20000;
                case '2':
                    return 50000;
                case '3':
                    return 100000;
                case '4':
                    return 125000;
                case '5':
                    return 250000;
                case '6':
                    return 500000;
                case '7':
                    return 800000;
                case '8':
                    return 1000000;
                default:
                    return 0;
            }
        }

        FLASHMEM Session::Session(IPort &port)
            : port_(port)
        {
        }

        FLASHMEM void Session::reply_ok()
        {
            port_.write(&CR, 1);
        }

        FLASHMEM void Session::reply_err()
        {
            port_.write(&BELL, 1);
        }

        FLASHMEM void Session::reset()
        {
            if (open_)
            {
                port_.close();
            }
            open_        = false;
            listen_only_ = true;
            timestamps_  = false;
            baud_        = DEFAULT_BAUD;
        }

        /* Ignored (but ACKed) in listen-only so python-can's periodic
         * keep-alives can't fault the bus. */
        FLASHMEM void
        Session::handle_transmit(const char *line, size_t len, bool ext)
        {
            uint8_t id_chars = ext ? 8 : 3;
            size_t  min_len  = 1u + id_chars + 1u; /* kind + id + dlc */
            if (len < min_len)
            {
                reply_err();
                return;
            }

            uint32_t id;
            if (!parse_hex(line + 1, id_chars, id))
            {
                reply_err();
                return;
            }

            uint32_t dlc;
            if (!parse_hex(line + 1 + id_chars, 1, dlc) || dlc > 8)
            {
                reply_err();
                return;
            }

            if (len < min_len + dlc * 2)
            {
                reply_err();
                return;
            }

            SniffFrame frame{};
            frame.id         = id;
            frame.len        = static_cast<uint8_t>(dlc);
            frame.flags      = ext ? SNIFF_FLAG_EXTENDED : 0;
            const char *data = line + min_len;
            for (uint32_t i = 0; i < dlc; i++)
            {
                uint32_t byte;
                if (!parse_hex(data + i * 2, 2, byte))
                {
                    reply_err();
                    return;
                }
                frame.data[i] = static_cast<uint8_t>(byte);
            }

            if (open_ && !listen_only_)
            {
                port_.transmit(frame);
            }

            reply_ok();
        }

        FLASHMEM void Session::handle(const char *line, size_t len)
        {
            if (len == 0)
            {
                reply_ok(); /* bare CR -- treat as no-op */
                return;
            }

            switch (line[0])
            {
                case 'S': /* set bitrate index */
                {
                    uint32_t b = len < 2 ? 0 : baud_for_index(line[1]);
                    if (open_ || b == 0)
                    {
                        reply_err();
                        return;
                    }
                    baud_ = b;
                    reply_ok();
                    return;
                }
                case 'O': /* open, normal mode */
                case 'L': /* open, listen-only */
                {
                    bool listen = line[0] == 'L';
                    if (open_ || !port_.open(baud_, listen))
                    {
                        reply_err();
                        return;
                    }
                    open_        = true;
                    listen_only_ = listen;
                    reply_ok();
                    return;
                }
                case 'C': /* close */
                    if (!open_)
                    {
                        reply_err();
                        return;
                    }
                    port_.close();
                    open_ = false;
                    reply_ok();
                    return;
                case 't':
                    handle_transmit(line, len, false);
                    return;
                case 'T':
                    handle_transmit(line, len, true);
                    return;
                case 'Z': /* timestamp on/off */
                    if (len < 2)
                    {
                        reply_err();
                        return;
                    }
                    timestamps_ = (line[1] == '1');
                    reply_ok();
                    return;
                case 'F': /* status flags -- none tracked */
                    port_.write("F00\r", 4);
                    return;
                case 'V': /* version */
                    port_.write("V0100\r", 6);
                    return;
                case 'N': /* serial number */
                    port_.write("NMQB1\r", 6);
                    return;
                default:
                    reply_err();
                    return;
            }
        }

    } // namespace slcan
} // namespace core
