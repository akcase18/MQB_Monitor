/**
 * @file uds_decoder.hpp
 * @brief Turns decoded UDS responses into core::SignalStore values.
 *
 * This was core::SignalStore::on_uds_response(), and the per-request
 * accumulator arrays behind it were SignalStore members. Both moved here so
 * SignalStore's only write path is set_at() -- a plain "somebody measured
 * this" setter with no idea where the number came from. That is what lets a
 * second product feed the same store from analog sensors, and what removes
 * core/vehicle's last dependency on core/can (SignalStore is reachable from
 * core/config via warning.hpp, so a UDS include here would have leaked all
 * the way into the persisted config schema's header).
 *
 * Owns no timers and no scheduling: core::SignalPoller decides what to ask
 * for and when, UdsClient::poll() drains the answers, and this turns each
 * one into a stored value.
 */
#pragma once

#include <stdint.h>

#include "signal_store.hpp"
#include "uds_binding.hpp"

namespace core
{

    class UdsDecoder
    {
        public:
            explicit UdsDecoder(SignalStore &store);

            /**
             * Feeds one decoded UDS response in: finds every binding whose
             * requests match (byte1, byte2), applies that request's
             * offset/scale, and once all of a signal's requests have
             * reported, runs its transform and writes the result to the
             * store.
             *
             * @param byte1  First UDS request byte the response is for.
             * @param byte2  Second UDS request byte the response is for.
             * @param raw    Undecoded payload value; offset/scale are
             *               applied here from the matching request.
             * @param now_ms Timestamp recorded against any value written.
             */
            void on_response(uint8_t  byte1,
                             uint8_t  byte2,
                             float    raw,
                             uint32_t now_ms);

        private:
            SignalStore &store_;

            float request_raw_[UDS_BINDING_COUNT][MAX_REQUESTS_PER_SIGNAL]
                = {};
            bool request_recv_[UDS_BINDING_COUNT][MAX_REQUESTS_PER_SIGNAL]
                = {};
    };

} // namespace core
