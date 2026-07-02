#include "uds_client.hpp"

namespace core
{

    namespace
    {
        constexpr uint8_t NUM_DATA_BYTES    = 0x03;
        constexpr uint8_t ENHANCE_DATA_REQ  = 0x22;
        constexpr uint8_t ENHANCE_DATA_RESP = 0x62;
        constexpr uint8_t ONE_BYTE_PAYLOAD  = 0x04;
    } // namespace

    UdsClient::UdsClient(ICanBus &bus, uint32_t tx_id, uint32_t rx_id)
        : bus_(bus)
        , tx_id_(tx_id)
        , rx_id_(rx_id)
    {
    }

    void UdsClient::send(const UdsRequestSpec &spec)
    {
        CanFrame frame {};
        frame.id      = tx_id_;
        frame.len     = 8;
        frame.data[0] = NUM_DATA_BYTES;
        frame.data[1] = ENHANCE_DATA_REQ;
        frame.data[2] = spec.byte1;
        frame.data[3] = spec.byte2;

        bus_.send(frame);
    }

    void UdsClient::poll(ResponseCallback cb, void *ctx)
    {
        CanFrame frame;
        while (bus_.receive(frame))
        {
            if (frame.id != rx_id_ || frame.len < 4
                || frame.data[1] != ENHANCE_DATA_RESP)
            {
                continue;
            }

            uint8_t byte1 = frame.data[2];
            uint8_t byte2 = frame.data[3];

            int16_t raw_data;
            if (frame.data[0] == ONE_BYTE_PAYLOAD)
            {
                raw_data = frame.data[4];
            }
            else
            {
                raw_data = static_cast<int16_t>((frame.data[4] << 8)
                                                | frame.data[5]);
            }

            cb(ctx, byte1, byte2, static_cast<float>(raw_data));
        }
    }

} // namespace core
