/**
 * @file uds_signal_source.hpp
 * @brief core::ISignalSource over a vehicle CAN bus: the UDS client, the
 * request scheduler and the response decoder, wired together and presented
 * as one object.
 *
 * Before this existed, src/main.cpp held all three separately, called two of
 * them in a fixed order every loop iteration, and routed responses through a
 * pair of file-scope callbacks that reached back into the composition
 * singleton to find their way home. All of that was UDS-specific knowledge
 * living in the one file that is supposed to be able to swap acquisition
 * wholesale. It is here now, and the composition root holds an
 * ISignalSource& without knowing what is behind it.
 */
#pragma once

#include <stdint.h>

#include "platform.hpp"
#include "signal_poller.hpp"
#include "signal_source.hpp"
#include "signal_store.hpp"
#include "uds_client.hpp"
#include "uds_decoder.hpp"

namespace core
{

    class UdsSignalSource : public ISignalSource
    {
        public:
            /**
             * @param bus   CAN bus to send requests on and read responses
             *              from.
             * @param clock Monotonic time source, for request pacing and
             *              value timestamps.
             * @param store Where decoded values land.
             * @param tx_id CAN ID requests are sent on.
             * @param rx_id CAN ID responses are expected on.
             */
            UdsSignalSource(ICanBus     &bus,
                            IClock      &clock,
                            SignalStore &store,
                            uint32_t     tx_id,
                            uint32_t     rx_id);

            void set_active(const uint16_t *signal_ids,
                            uint8_t         count) override;

            /**
             * Drains responses, then sends at most one request.
             *
             * Order matters and is the reason this is one call rather than
             * two: a response landing this iteration frees the poller's
             * single in-flight slot, so draining first lets tick() put the
             * next request on the wire in the same pass instead of a loop
             * later.
             */
            void tick(uint32_t now_ms) override;

        private:
            /**
             * UdsClient's callbacks are plain function pointers plus a void*
             * context, so these trampoline through `this` -- which is what
             * lets the routing be a private detail here instead of two
             * file-scope functions in main.cpp reaching for a singleton. */
            static void response_cb(void   *ctx,
                                    uint8_t byte1,
                                    uint8_t byte2,
                                    float   raw);
            static void negative_response_cb(void   *ctx,
                                             uint8_t service,
                                             uint8_t nrc);

            IClock      &clock_;
            UdsClient    client_;
            UdsDecoder   decoder_;
            SignalPoller poller_;
    };

} // namespace core
