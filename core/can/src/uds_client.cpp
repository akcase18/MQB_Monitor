#include "uds_client.hpp"

#include <string.h>

#include "mem_placement.hpp"

namespace core
{

    namespace
    {
        constexpr uint8_t NUM_DATA_BYTES    = 0x03;
        constexpr uint8_t ENHANCE_DATA_REQ  = 0x22;
        constexpr uint8_t ENHANCE_DATA_RESP = 0x62;
        constexpr uint8_t NEGATIVE_RESP     = 0x7F;
        /**
         * 0x62 + the 2 identifier bytes ahead of a response's data.
         */
        constexpr uint8_t RESP_HEADER_LEN  = 3;
        constexpr uint8_t MAX_SINGLE_FRAME = 7;
        constexpr uint8_t FIRST_FRAME_DATA = 6;

        /**
         * ISO-TP frame types: the high nibble of the first byte.
         */
        constexpr uint8_t PCI_SINGLE_FRAME      = 0x0;
        constexpr uint8_t PCI_FIRST_FRAME       = 0x1;
        constexpr uint8_t PCI_CONSECUTIVE_FRAME = 0x2;
        constexpr uint8_t PCI_FLOW_CONTROL      = 0x30;
        constexpr uint8_t FC_CONTINUE           = 0x0;
        constexpr uint8_t FC_OVERFLOW           = 0x2;

        /**
         * Consecutive frames the module may send before waiting for the
         * next flow control. The Teensy holds at most four received frames
         * between poll() calls (TeensyCanBus's four mailboxes), and a main
         * loop pass with an LVGL repaint in it is far longer than a burst
         * takes to arrive, so an unlimited burst would overwrite itself.
         * Three leaves a spare for an unrelated frame.
         */
        constexpr uint8_t ISO_TP_BLOCK_SIZE = 3;
    } // namespace

    bool uds_decode_field(const uint8_t  *payload,
                          uint8_t         len,
                          const UdsField &field,
                          float          &out)
    {
        if (len == 0)
        {
            return false;
        }

        if (field.len == 0)
        {
            if (len == 1)
            {
                out = static_cast<float>(payload[0]); /* unsigned 8-bit */
            }
            else
            {
                out = static_cast<float>(static_cast<int16_t>(
                    (payload[0] << 8) | payload[1])); /* signed 16-bit */
            }
            return true;
        }

        if (field.len > 4
            || static_cast<uint16_t>(field.start) + field.len > len)
        {
            return false;
        }

        uint32_t v = 0;
        for (uint8_t i = 0; i < field.len; i++)
        {
            uint8_t at = field.little_endian ? field.len - 1 - i : i;
            v          = (v << 8) | payload[field.start + at];
        }

        uint8_t width = static_cast<uint8_t>(field.len * 8);
        if (field.bit_len != 0)
        {
            uint32_t mask = field.bit_len >= 32
                                ? 0xFFFFFFFFu
                                : (1u << field.bit_len) - 1u;
            v     = (v >> field.bit_shift) & mask;
            width = field.bit_len;
        }

        if (field.is_signed && width < 32 && (v & (1u << (width - 1))) != 0)
        {
            v |= ~((1u << width) - 1u); /* sign-extend */
        }

        out = field.is_signed ? static_cast<float>(static_cast<int32_t>(v))
                              : static_cast<float>(v);
        return true;
    }

    UdsClient::UdsClient(ICanBus &bus, const UdsEcu *ecus, uint8_t num_ecus)
        : bus_(bus)
        , ecus_(ecus)
        , num_ecus_(num_ecus)
    {
    }

    void UdsClient::send(const UdsRequestSpec &spec)
    {
        if (spec.ecu >= num_ecus_)
        {
            return;
        }

        rx_active_ = false;

        CanFrame frame {};
        frame.id      = ecus_[spec.ecu].tx_id;
        frame.len     = 8;
        frame.data[0] = NUM_DATA_BYTES;
        frame.data[1] = ENHANCE_DATA_REQ;
        frame.data[2] = spec.byte1;
        frame.data[3] = spec.byte2;

        bus_.send(frame);
    }

    int UdsClient::ecu_for_rx(uint32_t rx_id) const
    {
        for (uint8_t i = 0; i < num_ecus_; i++)
        {
            if (ecus_[i].rx_id == rx_id)
            {
                return i;
            }
        }
        return -1;
    }

    void UdsClient::dispatch(uint8_t                  ecu,
                             const uint8_t           *sdu,
                             uint8_t                  len,
                             ResponseCallback         cb,
                             void                    *ctx,
                             NegativeResponseCallback ncb,
                             void                    *nctx)
    {
        if (len < 3)
        {
            return;
        }

        /**
         * `7F <service> <nrc>` -- a rejection, not a reading. It names no
         * data identifier, so it is passed straight through for the caller
         * to attribute to whatever it has outstanding (see
         * NegativeResponseCallback).
         */
        if (sdu[0] == NEGATIVE_RESP)
        {
            if (ncb != nullptr)
            {
                ncb(nctx, ecu, sdu[1], sdu[2]);
            }
            return;
        }

        if (sdu[0] == ENHANCE_DATA_RESP)
        {
            cb(ctx,
               ecu,
               sdu[1],
               sdu[2],
               &sdu[RESP_HEADER_LEN],
               static_cast<uint8_t>(len - RESP_HEADER_LEN));
        }
    }

    /**
     * FLASHMEM: only a DID too long for one frame comes this way, and RAM1
     * code space is the Teensy's scarcest resource (see mem_placement.hpp).
     */
    FLASHMEM void UdsClient::send_flow_control(uint8_t ecu, uint8_t status)
    {
        CanFrame frame {};
        frame.id      = ecus_[ecu].tx_id;
        frame.len     = 8;
        frame.data[0] = static_cast<uint8_t>(PCI_FLOW_CONTROL | status);
        frame.data[1] = status == FC_CONTINUE ? ISO_TP_BLOCK_SIZE : 0;
        frame.data[2] = 0; /* STmin: as fast as it likes, within a block */

        bus_.send(frame);
    }

    FLASHMEM void UdsClient::on_first_frame(uint8_t ecu, const CanFrame &frame)
    {
        uint16_t total = static_cast<uint16_t>(((frame.data[0] & 0x0F) << 8)
                                               | frame.data[1]);
        rx_active_ = false;

        /**
         * A first frame is only ever sent for more than a single frame
         * holds. Anything longer than the buffer is refused with an
         * overflow, so the module stops instead of sending the rest into
         * the void.
         */
        if (frame.len < 8 || total <= MAX_SINGLE_FRAME)
        {
            return;
        }
        if (total > sizeof(rx_buf_))
        {
            send_flow_control(ecu, FC_OVERFLOW);
            return;
        }

        memcpy(rx_buf_, &frame.data[2], FIRST_FRAME_DATA);
        rx_total_    = static_cast<uint8_t>(total);
        rx_len_      = FIRST_FRAME_DATA;
        rx_ecu_      = ecu;
        rx_seq_      = 1;
        rx_since_fc_ = 0;
        rx_active_   = true;
        send_flow_control(ecu, FC_CONTINUE);
    }

    FLASHMEM bool UdsClient::on_consecutive_frame(uint8_t         ecu,
                                                  const CanFrame &frame)
    {
        if (!rx_active_ || ecu != rx_ecu_)
        {
            return false;
        }

        /**
         * A missed or repeated frame leaves the payload with a hole in it:
         * abandon it, and the poller retries the request when it times out.
         */
        uint8_t remaining = static_cast<uint8_t>(rx_total_ - rx_len_);
        uint8_t chunk     = remaining < 7 ? remaining : 7;
        if ((frame.data[0] & 0x0F) != rx_seq_ || frame.len < chunk + 1)
        {
            rx_active_ = false;
            return false;
        }

        memcpy(&rx_buf_[rx_len_], &frame.data[1], chunk);
        rx_len_ = static_cast<uint8_t>(rx_len_ + chunk);
        rx_seq_ = static_cast<uint8_t>((rx_seq_ + 1) & 0x0F);

        if (rx_len_ >= rx_total_)
        {
            rx_active_ = false;
            return true;
        }

        if (++rx_since_fc_ == ISO_TP_BLOCK_SIZE)
        {
            rx_since_fc_ = 0;
            send_flow_control(ecu, FC_CONTINUE);
        }
        return false;
    }

    void UdsClient::poll(ResponseCallback         cb,
                         void                    *ctx,
                         NegativeResponseCallback ncb,
                         void                    *nctx)
    {
        CanFrame frame;
        while (bus_.receive(frame))
        {
            int ecu = ecu_for_rx(frame.id);
            if (ecu < 0 || frame.len < 1)
            {
                continue;
            }
            uint8_t module = static_cast<uint8_t>(ecu);

            switch (frame.data[0] >> 4)
            {
                case PCI_SINGLE_FRAME:
                {
                    /**
                     * The PCI byte gives the length. Only the first `len`
                     * bytes of `frame` are written by ICanBus::receive(),
                     * so a PCI length running past them would read
                     * whatever the previous iteration left there --
                     * dropped, not decoded.
                     */
                    uint8_t pci_len = frame.data[0] & 0x0F;
                    if (pci_len > MAX_SINGLE_FRAME || frame.len < pci_len + 1)
                    {
                        break;
                    }
                    dispatch(
                        module, &frame.data[1], pci_len, cb, ctx, ncb, nctx);
                    break;
                }

                case PCI_FIRST_FRAME:
                    on_first_frame(module, frame);
                    break;

                case PCI_CONSECUTIVE_FRAME:
                    if (on_consecutive_frame(module, frame))
                    {
                        dispatch(
                            module, rx_buf_, rx_total_, cb, ctx, ncb, nctx);
                    }
                    break;

                default:
                    break;
            }
        }
    }

} // namespace core
