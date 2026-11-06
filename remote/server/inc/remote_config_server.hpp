/**
 * @file remote_config_server.hpp
 * @brief Answers the ESP32-C3's config requests (REQ_GET_STATE,
 * REQ_PREVIEW_CONFIG, REQ_APPLY_CONFIG, REQ_REVERT_PREVIEW) against
 * core::ConfigStore — the Teensy-side counterpart to
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

            /** What a request just did to the live config -- see
             * ConfigCallback. */
            enum class ConfigEvent : uint8_t
            {
                /* A REQ_PREVIEW_CONFIG landed: the live config (and the
                 * preview values) changed but nothing was saved. */
                Previewed,
                /* A REQ_APPLY_CONFIG landed: the live config has been
                 * saved. A preview session, if any, carries on. */
                Committed,
                /* The preview session ended: the live config is back to
                 * what was last saved, and live values are back. */
                Reverted,
                /* As Reverted, but because the page went away (a
                 * REQ_REVERT_PREVIEW carrying REVERT_PAGE_LEFT) rather
                 * than the phone's Discard. */
                PageLeft,
            };

            using ConfigCallback = void (*)(void *ctx, ConfigEvent event);

            /**
             * Registers a callback fired whenever a request changes the
             * live config -- never on a rejected batch, which leaves it
             * untouched.
             *
             * Without this the config store changes underneath a UI that
             * has no idea: every screen is built once against the values
             * current at build time (see main.cpp's build_screens), so a
             * phone-side edit would not show up until the next boot. The
             * server itself stays free of any UI dependency -- it just
             * says what changed and lets the composition root decide what
             * that costs.
             */
            void set_config_callback(ConfigCallback cb, void *ctx);

            /**
             * True while the phone is driving the panel: from the first
             * REQ_PREVIEW_CONFIG until a revert ends it. A Save does not
             * end it -- the phone's page stays open and keeps showing its
             * mock, so the panel keeps matching it. While this is true the
             * live config may hold unsaved edits, so nothing else should
             * save() it.
             */
            bool preview_active() const
            {
                return preview_active_;
            }

            /** True when the live config holds previewed edits that have
             * not been saved. */
            bool has_unsaved_changes() const
            {
                return unsaved_;
            }

            /**
             * The values the phone's mock is showing, for the panel to
             * show in place of live ones so the two match -- or nullptr
             * outside a preview session. A signal the mock has no value
             * for has no data here, and reads as "--".
             */
            const core::SignalStore *preview_values() const
            {
                return preview_active_ ? &preview_values_ : nullptr;
            }

            /** Ends the preview session: restores the last saved config
             * and goes back to live values. No-op (and no callback) unless
             * preview_active(). For the composition root to call when the
             * phone goes away -- the AP going down, or the page falling
             * silent. */
            void revert_preview();

        private:
            size_t handle_get_state(uint8_t *out, size_t cap);
            /** REQ_PREVIEW_CONFIG/REQ_APPLY_CONFIG/REQ_REVERT_PREVIEW --
             * everything that answers with a RESP_APPLY_RESULT. */
            protocol::ApplyResult
            handle_config_change(protocol::MsgType req_type,
                                 const uint8_t    *payload,
                                 uint16_t          len);
            /** Validates `payload` as a whole batch and, only if all of
             * it passes, writes it into the live config. Shared by
             * REQ_PREVIEW_CONFIG and REQ_APPLY_CONFIG, which differ only
             * in what happens afterwards. `log_at` is where the optional
             * log profile would start; a body that ends there leaves the
             * profile alone. */
            protocol::ApplyResult apply_to_live(const uint8_t *payload,
                                                uint16_t       len,
                                                size_t         log_at);
            /** Reads the preview values that follow a REQ_PREVIEW_CONFIG's
             * config batch into preview_values_. */
            void read_preview_values(const uint8_t *p, size_t len);
            void notify(ConfigEvent event);
            /** revert_preview(), reporting `event`. */
            void end_preview(ConfigEvent event);

            core::ConfigStore &config_;
            core::SignalStore &signals_;
            ConfigCallback     config_cb_      = nullptr;
            void              *config_ctx_     = nullptr;
            bool               preview_active_ = false;
            bool               unsaved_        = false;
            core::SignalStore  preview_values_;
            /* The saved config, captured when a preview session begins
             * and refreshed by each Save within it. static for
             * the same reason as FrameReader::buf_ (DMAMEM placement needs
             * static storage; RAM1 is tight on Teensy) -- there is only
             * ever one RemoteConfigServer in the real app. On the ESP32 it
             * lives in PSRAM instead (see psram_new()). */
#if defined(ARDUINO_ARCH_ESP32)
            static core::Config &saved_before_preview_;
#else
            static core::Config saved_before_preview_;
#endif
    };

} // namespace remote::server
