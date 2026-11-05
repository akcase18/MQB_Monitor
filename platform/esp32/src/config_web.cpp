#include "config_web.hpp"

#include "web_index_gz.h" /* generated -- see tools/web/embed_web.py */

namespace esp32
{
    using remote::protocol::MsgType;
    namespace protocol = remote::protocol;

    namespace
    {
        /* Not on the loopTask stack, same reasoning as RelayLink's
         * frame_scratch_. Only one handler runs at a time. */
        LazyBuffer<uint8_t, protocol::FRAME_MAX_PAYLOAD> s_bytes;
        LazyBuffer<char, 2 * protocol::FRAME_MAX_PAYLOAD + 1> s_hex;

        void to_hex(const uint8_t *bytes, size_t len, char *out)
        {
            static const char DIGITS[] = "0123456789abcdef";
            for (size_t i = 0; i < len; i++)
            {
                out[2 * i]     = DIGITS[bytes[i] >> 4];
                out[2 * i + 1] = DIGITS[bytes[i] & 0x0F];
            }
            out[2 * len] = '\0';
        }

        int hex_nibble(char c)
        {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f')
            {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F')
            {
                return c - 'A' + 10;
            }
            return -1;
        }

        /** @returns The decoded length, or -1 if `hex` isn't an even run of
         * hex digits that fits in `cap` bytes. */
        int from_hex(const String &hex, uint8_t *out, size_t cap)
        {
            size_t len = hex.length();
            if (len % 2 != 0 || len / 2 > cap)
            {
                return -1;
            }
            for (size_t i = 0; i < len / 2; i++)
            {
                int hi = hex_nibble(hex[2 * i]);
                int lo = hex_nibble(hex[2 * i + 1]);
                if (hi < 0 || lo < 0)
                {
                    return -1;
                }
                out[i] = static_cast<uint8_t>((hi << 4) | lo);
            }
            return static_cast<int>(len / 2);
        }
    } // namespace

    ConfigWebHandler::ConfigWebHandler(RelayLink &relay)
        : relay_(relay)
    {
    }

    void ConfigWebHandler::attach(WebServer &server)
    {
        server_ = &server;
        server.on("/", HTTP_GET, [this]() { handle_page(); });
        server.on("/api/state", HTTP_GET, [this]() { handle_state(); });
        server.on("/api/preview", HTTP_POST, [this]() {
            handle_change(MsgType::REQ_PREVIEW_CONFIG);
        });
        server.on("/api/save", HTTP_POST, [this]() {
            handle_change(MsgType::REQ_APPLY_CONFIG);
        });
        server.on("/api/revert", HTTP_POST, [this]() {
            handle_change(MsgType::REQ_REVERT_PREVIEW);
        });
    }

    void ConfigWebHandler::handle_page()
    {
        /* no-store: a reflashed C3 must not have the phone keep running
         * the old page against a changed wire format. */
        server_->sendHeader("Cache-Control", "no-store");
        server_->sendHeader("Content-Encoding", "gzip");
        server_->send_P(200,
                        "text/html",
                        reinterpret_cast<PGM_P>(WEB_INDEX_GZ),
                        WEB_INDEX_GZ_LEN);
    }

    void ConfigWebHandler::handle_state()
    {
        uint8_t *bytes = s_bytes.get();
        char    *hex   = s_hex.get();
        if (bytes == nullptr || hex == nullptr)
        {
            server_->send(503, "text/plain", "out of memory");
            return;
        }

        uint16_t resp_len = 0;
        if (!relay_.send_request_and_wait(MsgType::REQ_GET_STATE,
                                          nullptr,
                                          0,
                                          MsgType::RESP_STATE,
                                          bytes,
                                          s_bytes.size(),
                                          resp_len))
        {
            server_->send(504, "text/plain", "relay timeout");
            return;
        }

        to_hex(bytes, resp_len, hex);
        server_->sendHeader("Cache-Control", "no-store");
        server_->send(200, "text/plain", hex);
    }

    void ConfigWebHandler::handle_change(MsgType type)
    {
        uint8_t *bytes = s_bytes.get();
        if (bytes == nullptr)
        {
            server_->send(503, "text/plain", "out of memory");
            return;
        }

        int len = from_hex(server_->arg("plain"), bytes, s_bytes.size());
        if (len < 0)
        {
            server_->send(400, "text/plain", "bad hex body");
            return;
        }

        uint8_t  resp[8];
        uint16_t resp_len = 0;
        if (!relay_.send_request_and_wait(type,
                                          bytes,
                                          static_cast<uint16_t>(len),
                                          MsgType::RESP_APPLY_RESULT,
                                          resp,
                                          sizeof(resp),
                                          resp_len)
            || resp_len < 1)
        {
            server_->send(504, "text/plain", "relay timeout");
            return;
        }

        char digits[4];
        snprintf(digits, sizeof(digits), "%u", resp[0]);
        server_->send(200, "text/plain", digits);
    }

} // namespace esp32
