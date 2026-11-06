/**
 * @file uds_client.hpp
 * @brief UDS enhanced-diagnostic-session request/response protocol over
 * ICanBus, to any of the modules in a caller-supplied ECU table. Owns no
 * timers and no request scheduling — core::SignalPoller decides what to ask
 * for and when, calls send() for it, and poll() is called once per
 * main-loop iteration to drain any responses. Response-to-signal matching
 * is left to the caller (core/vehicle), not scanned here.
 *
 * Responses longer than a single frame arrive as ISO-TP first and
 * consecutive frames: poll() reassembles them, sending the flow control
 * that lets the module carry on, up to UDS_MAX_PAYLOAD data bytes.
 */
#pragma once

#include <stdint.h>

#include "platform.hpp"

namespace core
{

    /**
     * Where a request's value sits in the response payload (the bytes after
     * 0x62 and the 2 identifier bytes). `len == 0` is the legacy auto
     * decode: u8 for a 1-byte payload, s16 from bytes 0-1. Otherwise
     * `len` bytes from `start`, big-endian unless `little_endian` (most
     * VW data is big-endian, but not all of it), optionally narrowed to
     * `bit_len` bits starting `bit_shift` bits up, sign-extended at the field's
     * width when `is_signed`. This is how several signals each read their own
     * value out of one multi-value DID. Mirrored by the monitor tool's
     * decode_field() (tools/monitor/mqb_monitor/catalog.py).
     */
    struct UdsField
    {
            uint8_t start         = 0;
            uint8_t len           = 0; /* bytes, 1-4; 0 = legacy auto */
            bool    is_signed     = false;
            uint8_t bit_shift     = 0;
            uint8_t bit_len       = 0; /* 0 = the whole field */
            bool    little_endian = false;
    };

    /**
     * One module on the bus: the CAN id requests go to it on, and the one
     * it answers on.
     */
    struct UdsEcu
    {
            uint16_t tx_id;
            uint16_t rx_id;
    };

    struct UdsRequestSpec
    {
            uint8_t  byte1;
            uint8_t  byte2;
            float    offset;
            float    scale;
            UdsField field;
            /**
             * Index into the ECU table UdsClient was built with: which
             * module this request is for.
             */
            uint8_t ecu;
    };

    /**
     * Longest response payload (after 0x62 and the identifier) poll()
     * reassembles. A longer answer is refused with an overflow flow
     * control.
     */
    constexpr uint8_t UDS_MAX_PAYLOAD = 32;

    /**
     * Decodes `field` from `payload` (`len` bytes) into `out`.
     *
     * @returns False if the payload is empty or too short for the field.
     */
    bool uds_decode_field(const uint8_t  *payload,
                          uint8_t         len,
                          const UdsField &field,
                          float          &out);

    class UdsClient
    {
        public:
            /**
             * @param ecu     Index of the module that answered, in the ECU
             *                table this client was built with.
             * @param byte1   First UDS request byte the response is for.
             * @param byte2   Second UDS request byte the response is for.
             * @param payload The response's data bytes, after the
             *                identifier. Nothing is decoded here: each
             *                catalog request owning (byte1, byte2) reads its
             *                own field out of it (uds_decode_field()), since
             *                one DID can carry several signals' values.
             * @param len     Bytes in `payload`.
             */
            using ResponseCallback = void (*)(void          *ctx,
                                              uint8_t        ecu,
                                              uint8_t        byte1,
                                              uint8_t        byte2,
                                              const uint8_t *payload,
                                              uint8_t        len);

            /**
             * A negative response (service 0x7F). Carries no data
             * identifier — the ECU only echoes which service it is
             * rejecting — so only a caller tracking its own outstanding
             * request can say which DID this belongs to. That is exactly
             * what core::SignalPoller does with it: an NRC is a request
             * finishing badly, and seeing it frees the in-flight slot
             * immediately instead of waiting out a full timeout.
             *
             * @param ecu     Index of the module that rejected it.
             * @param service The rejected service id (0x22 for the
             *                read-data-by-identifier requests send() makes).
             * @param nrc     ISO 14229 negative response code. 0x78
             *                (requestCorrectlyReceived-ResponsePending) is
             *                the one that does NOT mean "done".
             */
            using NegativeResponseCallback = void (*)(void   *ctx,
                                                      uint8_t ecu,
                                                      uint8_t service,
                                                      uint8_t nrc);

            /**
             * @param bus      CAN bus to send/receive on.
             * @param ecus     Every module requests can go to; a response
             *                 on any other CAN id is ignored. Must outlive
             *                 the client.
             * @param num_ecus Length of `ecus`.
             */
            UdsClient(ICanBus &bus, const UdsEcu *ecus, uint8_t num_ecus);

            /**
             * Sends a UDS enhanced-data-service request for the given spec's
             * (byte1, byte2) identifier pair to the spec's module. Drops any
             * long response still being reassembled: only one request is
             * outstanding at a time, so it belongs to an abandoned one.
             */
            void send(const UdsRequestSpec &spec);

            /**
             * @returns True while a multi-frame response is part-way in --
             * core::SignalPoller keeps waiting past its usual deadline
             * rather than giving up on an answer that is arriving.
             */
            bool receiving() const
            {
                return rx_active_;
            }

            /**
             * Drains every pending response on the bus, invoking `cb` with
             * each one's payload. Negative responses go to `ncb` when one
             * is supplied, and are dropped when it is null.
             */
            void poll(ResponseCallback         cb,
                      void                    *ctx,
                      NegativeResponseCallback ncb  = nullptr,
                      void                    *nctx = nullptr);

        private:
            int  ecu_for_rx(uint32_t rx_id) const;
            void dispatch(uint8_t                  ecu,
                          const uint8_t           *sdu,
                          uint8_t                  len,
                          ResponseCallback         cb,
                          void                    *ctx,
                          NegativeResponseCallback ncb,
                          void                    *nctx);
            void on_first_frame(uint8_t ecu, const CanFrame &frame);
            bool on_consecutive_frame(uint8_t ecu, const CanFrame &frame);
            void send_flow_control(uint8_t ecu, uint8_t status);

            ICanBus      &bus_;
            const UdsEcu *ecus_;
            uint8_t       num_ecus_;

            /**
             * The multi-frame response being reassembled: 0x62, the two
             * identifier bytes, then up to UDS_MAX_PAYLOAD of data.
             */
            uint8_t rx_buf_[3 + UDS_MAX_PAYLOAD] = {};
            uint8_t rx_total_                    = 0;
            uint8_t rx_len_                      = 0;
            uint8_t rx_ecu_                      = 0;
            uint8_t rx_seq_                      = 0;
            uint8_t rx_since_fc_                 = 0;
            bool    rx_active_                   = false;
    };

} // namespace core
