#include "remote_config_link.hpp"

namespace remote::server
{
    using protocol::MsgType;

    DMAMEM uint8_t RemoteConfigLink::resp_payload_[protocol::FRAME_MAX_PAYLOAD];

    FLASHMEM RemoteConfigLink::RemoteConfigLink(ISerialLink        &link,
                                                RemoteConfigServer &server)
        : link_(link)
        , server_(server)
    {
    }

    FLASHMEM void RemoteConfigLink::write_frame(MsgType        type,
                                                const uint8_t *payload,
                                                uint16_t       len)
    {
        uint8_t  header[4];
        uint8_t *hp = header;
        hp          = protocol::put_u8(hp, protocol::FRAME_SYNC);
        hp          = protocol::put_u8(hp, static_cast<uint8_t>(type));
        hp          = protocol::put_u16(hp, len);
        link_.write(header, sizeof(header));

        if (len > 0)
        {
            link_.write(payload, len);
        }

        uint16_t crc = protocol::crc16_ccitt_continue(0xFFFF, &header[1], 3);
        crc          = protocol::crc16_ccitt_continue(crc, payload, len);

        uint8_t crc_bytes[2];
        protocol::put_u16(crc_bytes, crc);
        link_.write(crc_bytes, sizeof(crc_bytes));
    }

    FLASHMEM void RemoteConfigLink::poll(EventCallback evt_cb,
                                         void         *evt_ctx,
                                         uint32_t      now_ms)
    {
        uint8_t chunk[64];
        size_t  n;
        while ((n = link_.read(chunk, sizeof(chunk))) > 0)
        {
            for (size_t i = 0; i < n; i++)
            {
                if (!reader_.on_byte(chunk[i], now_ms))
                {
                    continue;
                }

                const protocol::DecodedFrame &frame = reader_.decoded_frame();

                switch (frame.type)
                {
                    case MsgType::REQ_GET_STATE:
                    case MsgType::REQ_APPLY_CONFIG: {
                        MsgType resp_type;
                        size_t  resp_len
                            = server_.handle_request(frame.type,
                                                     frame.payload,
                                                     frame.payload_len,
                                                     resp_payload_,
                                                     sizeof(resp_payload_),
                                                     resp_type);
                        write_frame(resp_type,
                                    resp_payload_,
                                    static_cast<uint16_t>(resp_len));
                        break;
                    }

                    default:
                        if (evt_cb != nullptr)
                        {
                            evt_cb(evt_ctx,
                                   frame.type,
                                   frame.payload,
                                   frame.payload_len);
                        }
                        break;
                }
            }
        }
    }

    FLASHMEM void RemoteConfigLink::send_command(MsgType        type,
                                                 const uint8_t *payload,
                                                 uint16_t       len)
    {
        write_frame(type, payload, len);
    }

} // namespace remote::server
