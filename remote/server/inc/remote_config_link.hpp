/**
 * @file remote_config_link.hpp
 * @brief Glue between raw ISerialLink bytes and RemoteConfigServer/AP
 * events. poll() drains available bytes through a protocol::FrameReader —
 * REQ_* frames dispatch straight to RemoteConfigServer with the RESP_*
 * written back out immediately; anything else (EVT_*) goes to a
 * caller-supplied callback. send_command() is the fire-and-forget
 * counterpart for CMD_AP_ENABLE/CMD_AP_DISABLE. Same non-blocking,
 * called-once-per-loop-iteration shape as core::can::UdsClient::poll().
 */
#pragma once

#include "platform.hpp"
#include "remote_config_server.hpp"
#include "remote_protocol.hpp"

namespace remote::server
{
    class RemoteConfigLink
    {
        public:
            using EventCallback = void (*)(void             *ctx,
                                           protocol::MsgType type,
                                           const uint8_t    *payload,
                                           uint16_t          len);

            RemoteConfigLink(ISerialLink &link, RemoteConfigServer &server);

            /**
             * Call once per main-loop iteration.
             */
            void poll(EventCallback evt_cb, void *evt_ctx, uint32_t now_ms);

            /**
             * Fire-and-forget outbound command/frame — no reply is
             * awaited here (CMD_AP_ENABLE/CMD_AP_DISABLE's async replies,
             * EVT_AP_READY/EVT_AP_DISABLED, arrive via poll()'s
             * EventCallback instead).
             */
            void send_command(protocol::MsgType type,
                              const uint8_t    *payload,
                              uint16_t          len);

        private:
            /**
             * Streams a frame straight to `link_` rather than building it
             * in a second full-size buffer first — the CRC is chained
             * across the header and payload separately (see
             * protocol::crc16_ccitt_continue()) instead of requiring both
             * to be contiguous in memory.
             */
            void write_frame(protocol::MsgType type,
                             const uint8_t    *payload,
                             uint16_t          len);

            /**
             * resp_payload_'s storage; null only if the ESP32 build's
             * allocation failed.
             */
            uint8_t *resp_payload();

            ISerialLink          &link_;
            RemoteConfigServer   &server_;
            protocol::FrameReader reader_;
            /**
             * Scratch space for a response payload built by
             * RemoteConfigServer before write_frame() streams it out —
             * sized for the largest response (RESP_STATE). static on the
             * Teensy for the same reason as FrameReader::buf_ (DMAMEM
             * needs static storage duration; only one RemoteConfigLink is
             * ever alive in the real app); a LazyBuffer on the ESP32, so
             * it sits in PSRAM on the S3. Always fully overwritten up to the returned length before
             * being read, so it doesn't need DMAMEM's .bss.dma section to
             * guarantee zero-initialization at startup.
             */
#if defined(ARDUINO_ARCH_ESP32)
            LazyBuffer<uint8_t, protocol::FRAME_MAX_PAYLOAD> resp_payload_;
#else
            static uint8_t resp_payload_[protocol::FRAME_MAX_PAYLOAD];
#endif
    };

} // namespace remote::server
