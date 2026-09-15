#include "uds_decoder.hpp"

namespace core
{

    UdsDecoder::UdsDecoder(SignalStore &store)
        : store_(store)
    {
    }

    void UdsDecoder::on_response(uint8_t  byte1,
                                 uint8_t  byte2,
                                 float    raw,
                                 uint32_t now_ms)
    {
        for (size_t i = 0; i < UDS_BINDING_COUNT; i++)
        {
            const UdsSignalBinding &binding = UDS_BINDINGS[i];

            /**
             * A signal that owns none of this response's requests can't
             * have changed state, so there's nothing to re-evaluate for
             * it -- skipping straight past keeps the per-frame cost
             * proportional to how many signals actually match rather
             * than re-walking every entry's requests twice. */
            bool matched = false;
            for (uint8_t r = 0; r < binding.num_requests; r++)
            {
                const UdsRequestSpec &req = binding.requests[r];
                if (req.byte1 != byte1 || req.byte2 != byte2)
                {
                    continue;
                }

                request_raw_[i][r]  = (raw + req.offset) * req.scale;
                request_recv_[i][r] = true;
                matched             = true;
            }

            if (!matched)
            {
                continue;
            }

            bool all_received = true;
            for (uint8_t r = 0; r < binding.num_requests; r++)
            {
                if (!request_recv_[i][r])
                {
                    all_received = false;
                    break;
                }
            }

            if (all_received)
            {
                /**
                 * set_at(i), not set(binding.signal_id) -- UDS_BINDINGS is
                 * a parallel array to SIGNAL_CATALOG (see uds_binding.hpp),
                 * so the catalog index is already in hand here and
                 * re-deriving it from the id would be a pointless second
                 * scan. */
                store_.set_at(
                    static_cast<int>(i),
                    binding.transform(request_raw_[i], binding.num_requests),
                    now_ms);
            }
        }
    }

} // namespace core
