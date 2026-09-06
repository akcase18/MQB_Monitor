/**
 * @file uds_client.hpp
 * @brief UDS enhanced-diagnostic-session request/response protocol over
 * ICanBus. Owns no timers and no request scheduling — core::SignalPoller
 * decides what to ask for and when, calls send() for it, and poll() is
 * called once per main-loop iteration to drain and decode any responses.
 * Response-to-signal matching is left to the caller (core/vehicle), not
 * scanned here.
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
             * A negative response (service 0x7F). Carries no data
             * identifier — the ECU only echoes which service it is
             * rejecting — so only a caller tracking its own outstanding
             * request can say which DID this belongs to. That is exactly
             * what core::SignalPoller does with it: an NRC is a request
             * finishing badly, and seeing it frees the in-flight slot
             * immediately instead of waiting out a full timeout.
             *
             * @param service The rejected service id (0x22 for the
             *                read-data-by-identifier requests send() makes).
             * @param nrc     ISO 14229 negative response code. 0x78
             *                (requestCorrectlyReceived-ResponsePending) is
             *                the one that does NOT mean "done".
             */
            using NegativeResponseCallback
                = void (*)(void *ctx, uint8_t service, uint8_t nrc);

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
             * invoking `cb` for it. Negative responses go to `ncb` when one
             * is supplied, and are dropped when it is null.
             */
            void poll(ResponseCallback         cb,
                      void                    *ctx,
                      NegativeResponseCallback ncb  = nullptr,
                      void                    *nctx = nullptr);

        private:
            ICanBus &bus_;
            uint32_t tx_id_;
            uint32_t rx_id_;
    };

} // namespace core
