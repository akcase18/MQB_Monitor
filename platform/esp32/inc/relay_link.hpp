/**
 * @file relay_link.hpp
 * @brief The C3-side counterpart to remote::server::RemoteConfigLink — feeds
 * bytes from the UART link to the Teensy through a remote::protocol::
 * FrameReader and hands complete frames to a callback. CMD_AP_ENABLE/
 * CMD_AP_DISABLE are dispatched through poll() (see platform/esp32/src/
 * main.cpp); REQ_GET_STATE/REQ_APPLY_CONFIG (C3 -> Teensy, for the
 * phone-facing config form, see config_web.hpp) go through the separate
 * blocking send_request_and_wait() below instead — an HTTP handler has to
 * return a response body synchronously, so it can't wait for poll() to
 * come back around on a later loop() iteration the way EVT_AP_READY does.
 */
#pragma once

#include <Stream.h>

#include "remote_protocol.hpp"

namespace esp32
{
    class RelayLink
    {
        public:
            using FrameCallback = void (*)(void                     *ctx,
                                           remote::protocol::MsgType type,
                                           const uint8_t            *payload,
                                           uint16_t                  len);

            explicit RelayLink(Stream &stream);

            /** Call once per loop() iteration. */
            void poll(FrameCallback cb, void *ctx, uint32_t now_ms);

            /** Wraps `payload` into a full frame and writes it straight to
             * the stream — the outbound counterpart to send_command() on
             * the Teensy side, used here for EVT_AP_READY/EVT_AP_DISABLED. */
            void send_event(remote::protocol::MsgType type,
                            const uint8_t            *payload,
                            uint16_t                  len);

            /** Sends `req_type`/`req_payload`, then blocks — busy-polling
             * `stream_` for incoming bytes, yielding via delay(1) between
             * checks so the WiFi/lwIP background task isn't starved into
             * tripping the idle-task watchdog — until a frame of
             * `expected_resp_type` arrives, `timeout_ms` elapses, or a
             * frame of any other type arrives (treated as a desync, since
             * the Teensy only ever replies to one outstanding request at a
             * time). Feeds bytes through the same `reader_` poll() uses —
             * never call this and poll() in the same call stack. Only
             * safe from inside a WebServer handler invoked from loop(),
             * which by construction never overlaps a poll() call.
             *
             * @returns true and fills resp_buf/resp_len on success; false
             * on timeout, desync, or resp_cap too small for the reply.
             */
            bool send_request_and_wait(
                remote::protocol::MsgType req_type,
                const uint8_t            *req_payload,
                uint16_t                  req_len,
                remote::protocol::MsgType expected_resp_type,
                uint8_t                  *resp_buf,
                size_t                    resp_cap,
                uint16_t                 &resp_len,
                uint32_t                  timeout_ms = 800);

        private:
            Stream                       &stream_;
            remote::protocol::FrameReader reader_;

            /**
             * Scratch space for the outbound request frame built by
             * send_request_and_wait() — static rather than a stack array
             * for the same reason send_event() itself doesn't stack-
             * allocate more than once: a handler building a multi-KB HTML
             * response on the same loopTask stack while also holding a
             * FRAME_MAX_PAYLOAD+8 (1544-byte) request buffer AND resp_buf
             * is a real overflow risk on a C3. */
            static uint8_t
                req_scratch_buf_[remote::protocol::FRAME_MAX_PAYLOAD + 8];
    };

} // namespace esp32
