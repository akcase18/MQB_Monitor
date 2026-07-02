/**
 * @file uds_client.hpp
 * @brief UDS enhanced-diagnostic-session request/response protocol over
 * ICanBus. Owns no timers — a core::Scheduler task calls send() at each
 * signal's cycle time, and poll() is called once per main-loop iteration
 * to drain and decode any responses. Response-to-signal matching is left
 * to the caller (core/vehicle), not scanned here.
 */
#pragma once

#include <stdint.h>

#include "platform.hpp"

namespace core
{

    struct UdsRequestSpec
    {
            uint8_t byte1;
            uint8_t byte2;
            float   offset;
            float   scale;
    };

    class UdsClient
    {
        public:
            /**
             * @param byte1 First UDS request byte the response is for.
             * @param byte2 Second UDS request byte the response is for.
             * @param raw   Decoded raw payload value. Offset/scale are not
             *              applied here — the caller looks those up from
             *              whichever catalog entry owns this (byte1, byte2)
             *              pair.
             */
            using ResponseCallback
                = void (*)(void *ctx, uint8_t byte1, uint8_t byte2, float raw);

            /**
             * @param bus    CAN bus to send/receive on.
             * @param tx_id  CAN ID requests are sent on.
             * @param rx_id  CAN ID responses are expected on.
             */
            UdsClient(ICanBus &bus, uint32_t tx_id, uint32_t rx_id);

            /**
             * Sends a UDS enhanced-data-service request for the given spec's
             * (byte1, byte2) identifier pair.
             */
            void send(const UdsRequestSpec &spec);

            /**
             * Drains every pending response on the bus, decoding each one and
             * invoking `cb` for it.
             */
            void poll(ResponseCallback cb, void *ctx);

        private:
            ICanBus &bus_;
            uint32_t tx_id_;
            uint32_t rx_id_;
    };

} // namespace core
