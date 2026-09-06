#include "uds_client.hpp"

namespace core
{

    namespace
    {
        constexpr uint8_t NUM_DATA_BYTES    = 0x03;
        constexpr uint8_t ENHANCE_DATA_REQ  = 0x22;
        constexpr uint8_t ENHANCE_DATA_RESP = 0x62;
        constexpr uint8_t NEGATIVE_RESP     = 0x7F;
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

    void UdsClient::poll(ResponseCallback         cb,
                         void                    *ctx,
                         NegativeResponseCallback ncb,
                         void                    *nctx)
    {
        CanFrame frame;
        while (bus_.receive(frame))
        {
            if (frame.id != rx_id_ || frame.len < 4)
            {
                continue;
            }

            /* `03 7F <service> <nrc>` -- a rejection, not a reading. It
             * names no data identifier, so it is passed straight through
             * for the caller to attribute to whatever it has outstanding
             * (see NegativeResponseCallback). */
            if (frame.data[1] == NEGATIVE_RESP)
            {
                if (ncb != nullptr)
                {
                    ncb(nctx, frame.data[2], frame.data[3]);
                }
                continue;
            }

            if (frame.data[1] != ENHANCE_DATA_RESP)
            {
                continue;
            }

            uint8_t byte1 = frame.data[2];
            uint8_t byte2 = frame.data[3];

            /* Only the first `len` bytes of `frame` are written by
             * ICanBus::receive(), so a payload byte past that is whatever
             * the previous iteration left there. Real ECU responses are
             * always full 8-byte frames, but a short one used to be
             * decoded against a stale byte rather than dropped: a
             * one-byte payload needs data[4], a two-byte payload
             * data[4..5]. */
            uint8_t needed = (frame.data[0] == ONE_BYTE_PAYLOAD) ? 5 : 6;
            if (frame.len < needed)
            {
                continue;
            }

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
