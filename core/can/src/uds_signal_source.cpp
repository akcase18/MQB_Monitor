#include "uds_signal_source.hpp"

namespace core
{

    UdsSignalSource::UdsSignalSource(ICanBus     &bus,
                                     IClock      &clock,
                                     SignalStore &store,
                                     uint32_t     tx_id,
                                     uint32_t     rx_id)
        : clock_(clock)
        , client_(bus, tx_id, rx_id)
        , decoder_(store)
        , poller_(client_, clock)
    {
    }

    void UdsSignalSource::set_active(const uint16_t *signal_ids, uint8_t count)
    {
        poller_.set_active(signal_ids, count);
    }

    void UdsSignalSource::tick(uint32_t now_ms)
    {
        /**
         * Deliberately unused. Both members below already hold the same
         * IClock and read it themselves -- the poller to pace requests, the
         * decoder (via response_cb) to timestamp values. Timestamping off
         * `now_ms` instead would move every value's last_update_ms by
         * however long the rest of the loop iteration took, which
         * core::Logger's on-change spacing would see. The parameter is on
         * the interface for sources that have no clock of their own. */
        (void)now_ms;

        /* Drain first, send second -- see the header for why. */
        client_.poll(response_cb, this, negative_response_cb, this);
        poller_.tick();
    }

    void UdsSignalSource::response_cb(void   *ctx,
                                      uint8_t byte1,
                                      uint8_t byte2,
                                      float   raw)
    {
        auto *self = static_cast<UdsSignalSource *>(ctx);

        self->decoder_.on_response(byte1, byte2, raw, self->clock_.millis());

        /**
         * Closes the flow-control loop: this is what frees the poller's
         * single in-flight slot so the next request can go out. */
        self->poller_.on_response(byte1, byte2);
    }

    void UdsSignalSource::negative_response_cb(void   *ctx,
                                               uint8_t service,
                                               uint8_t nrc)
    {
        /**
         * A rejection still ends the request it rejects, so handing these to
         * the poller is what stops a DID the ECU refuses from costing a full
         * response timeout every time it comes due. */
        auto *self = static_cast<UdsSignalSource *>(ctx);
        self->poller_.on_negative_response(service, nrc);
    }

} // namespace core
