#include "relay_link.hpp"

#include <Arduino.h>
#include <string.h>

namespace esp32
{
    using remote::protocol::MsgType;

    RelayLink::RelayLink(Stream &stream)
        : stream_(stream)
    {
    }

    void RelayLink::poll(FrameCallback cb, void *ctx, uint32_t now_ms)
    {
        while (stream_.available() > 0)
        {
            auto b = static_cast<uint8_t>(stream_.read());
            if (!reader_.on_byte(b, now_ms))
            {
                continue;
            }

            const remote::protocol::DecodedFrame &frame
                = reader_.decoded_frame();
            if (cb != nullptr)
            {
                cb(ctx, frame.type, frame.payload, frame.payload_len);
            }
        }
    }

    void RelayLink::send_event(MsgType        type,
                               const uint8_t *payload,
                               uint16_t       len)
    {
        uint8_t *buf = frame_scratch_.get();
        if (buf == nullptr)
        {
            return;
        }
        size_t written = remote::protocol::encode_frame(
            type, payload, len, buf, frame_scratch_.size());
        if (written > 0)
        {
            stream_.write(buf, written);
        }
    }

    bool RelayLink::send_request_and_wait(MsgType        req_type,
                                          const uint8_t *req_payload,
                                          uint16_t       req_len,
                                          MsgType        expected_resp_type,
                                          uint8_t       *resp_buf,
                                          size_t         resp_cap,
                                          uint16_t      &resp_len,
                                          uint32_t       timeout_ms)
    {
        uint8_t *buf = frame_scratch_.get();
        if (buf == nullptr)
        {
            return false;
        }
        size_t written = remote::protocol::encode_frame(
            req_type, req_payload, req_len, buf, frame_scratch_.size());
        if (written == 0)
        {
            return false;
        }
        stream_.write(buf, written);

        uint32_t start_ms = millis();
        while (millis() - start_ms < timeout_ms)
        {
            if (stream_.available() <= 0)
            {
                delay(1);
                continue;
            }

            auto b = static_cast<uint8_t>(stream_.read());
            if (!reader_.on_byte(b, millis()))
            {
                continue;
            }

            const remote::protocol::DecodedFrame &frame
                = reader_.decoded_frame();
            if (frame.type != expected_resp_type)
            {
                /**
                 * Desync -- the Teensy only ever replies to one
                 * outstanding request at a time, so anything else here
                 * means this reply doesn't belong to this request.
                 */
                return false;
            }
            if (frame.payload_len > resp_cap)
            {
                return false;
            }

            memcpy(resp_buf, frame.payload, frame.payload_len);
            resp_len = frame.payload_len;
            return true;
        }

        return false;
    }

} // namespace esp32
