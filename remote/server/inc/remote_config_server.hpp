/**
 * @file remote_config_server.hpp
 * @brief Answers REQ_GET_STATE/REQ_APPLY_CONFIG requests from the ESP32-C3
 * against core::ConfigStore — the Teensy-side counterpart to
 * gui::VehicleBinding, but for the wireless-config wire format instead of
 * LVGL view-models. Pure bytes-in/bytes-out (no I/O of its own), so it's
 * host-testable exactly like the rest of core/.
 */
#pragma once

#include "config_store.hpp"
#include "remote_protocol.hpp"
#include "signal_store.hpp"

namespace remote::server
{
    class RemoteConfigServer
    {
        public:
            RemoteConfigServer(core::ConfigStore &config,
                               core::SignalStore &signals);

            /**
             * Handles one decoded request frame's payload and writes the
             * response payload (not yet frame-wrapped — see
             * protocol::encode_frame()) into `resp_buf`.
             *
             * @param req_type Request message type.
             * @param req_payload Request payload bytes.
             * @param req_len Request payload length.
             * @param resp_buf Buffer to write the response payload into.
             * @param resp_cap Capacity of `resp_buf`.
             * @param resp_type Set to the response's message type
             * (RESP_STATE/RESP_APPLY_RESULT/RESP_ERROR).
             *
             * @returns Response payload length written to `resp_buf`.
             */
            size_t handle_request(protocol::MsgType  req_type,
                                  const uint8_t     *req_payload,
                                  uint16_t           req_len,
                                  uint8_t           *resp_buf,
                                  size_t             resp_cap,
                                  protocol::MsgType &resp_type);

            using AppliedCallback = void (*)(void *ctx);

            /**
             * Registers a callback fired after a REQ_APPLY_CONFIG has
             * validated, committed to core::Config and been saved --
             * i.e. only on ApplyResult::Ok, never on a rejected batch.
             *
             * Without this the config store changes underneath a UI that
             * has no idea: every screen is built once against the values
             * current at build time (see main.cpp's build_screens), so a
             * phone-side Save would not show up until the next boot. The
             * server itself stays free of any UI dependency -- it just
             * says "the config changed" and lets the composition root
             * decide what that costs.
             */
            void set_applied_callback(AppliedCallback cb, void *ctx);

        private:
            size_t handle_get_state(uint8_t *out, size_t cap);
            size_t handle_apply_config(const uint8_t     *payload,
                                       uint16_t           len,
                                       uint8_t           *out,
                                       size_t             cap,
                                       protocol::MsgType &resp_type);

            core::ConfigStore &config_;
            core::SignalStore &signals_;
            AppliedCallback    applied_cb_  = nullptr;
            void              *applied_ctx_ = nullptr;
    };

} // namespace remote::server
