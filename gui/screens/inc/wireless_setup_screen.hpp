/**
 * @file wireless_setup_screen.hpp
 * @brief Wi-Fi hotspot setup: opening this screen brings up the ESP32's
 * AP immediately (no separate Activate step) and shows a spinner until an
 * lv_qrcode Wi-Fi-join code is ready.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{
    enum class WirelessSetupPhase : uint8_t
    {
        Idle,       /* Not yet activated. */
        Activating, /* Waiting on the ESP32. */
        Active      /* AP is up. */
    };

    constexpr uint8_t WIRELESS_QR_PAYLOAD_LEN = 128;
    constexpr uint8_t WIRELESS_SSID_LEN       = 33;
    constexpr uint8_t WIRELESS_URL_LEN        = 40;

    struct WirelessSetupViewModel
    {
            WirelessSetupPhase phase = WirelessSetupPhase::Idle;

            /**
             * "http://<ip>": opens the config site, same value as
             * url_text below.
             */
            char qr_payload[WIRELESS_QR_PAYLOAD_LEN] = "";

            /**
             * Shown as text underneath the QR code.
             */
            char ssid[WIRELESS_SSID_LEN] = "";

            /**
             * "http://<ip>": for anyone who's already joined the
             * network.
             */
            char url_text[WIRELESS_URL_LEN] = "";
    };

    class WirelessSetupScreen
    {
        public:
            using DeactivateCallback = void (*)(void *ctx);

            void init(const Theme       &theme,
                      DeactivateCallback deactivate_cb,
                      void              *deactivate_ctx);

            /**
             * Opens the screen showing `vm`'s current phase.
             */
            void open(const WirelessSetupViewModel &vm,
                      lv_obj_t                     *return_screen);

            /**
             * Re-pulls the current phase/QR payload without changing
             * screens.
             */
            void refresh(const WirelessSetupViewModel &vm);

            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            static void close_clicked_cb(lv_event_t *e);

            void set_view_model(const WirelessSetupViewModel &vm);

            lv_obj_t *screen_        = nullptr;
            lv_obj_t *return_screen_ = nullptr;
            /**
             * Spinner + status text, shown for both Idle and Activating.
             */
            lv_obj_t *connecting_group_ = nullptr;
            lv_obj_t *active_group_     = nullptr;
            lv_obj_t *qrcode_           = nullptr;
            lv_obj_t *ssid_lbl_         = nullptr;
            lv_obj_t *url_lbl_          = nullptr;

            WirelessSetupPhase last_phase_ = WirelessSetupPhase::Idle;

            DeactivateCallback deactivate_cb_  = nullptr;
            void              *deactivate_ctx_ = nullptr;
    };

} // namespace gui
