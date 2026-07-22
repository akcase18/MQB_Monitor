/**
 * @file wireless_setup_screen.hpp
 * @brief Wi-Fi hotspot setup: opening this screen brings up the ESP32-C3's
 * AP immediately (no separate Activate step) and shows a spinner until an
 * lv_qrcode Wi-Fi-join code is ready, so the rest of config (panel/signal/
 * threshold editing) can happen from a phone browser instead of this
 * touchscreen. Like every other gui/screens screen, this one is driven
 * entirely by a POD view-model (WirelessSetupViewModel) and has zero
 * remote::/core:: dependency — gui::RemoteBinding (gui/bindings) is what
 * actually talks to remote::server::RemoteApController, and main.cpp
 * calls RemoteBinding::activate() at the same point it calls open() here.
 *
 * The close (X) button is the only way out, and it deactivates the AP on
 * the way — there's no separate "Turn Off" control. It does so
 * optimistically: it fires DeactivateCallback and returns to Settings
 * immediately rather than waiting to watch ApState reach Idle (that
 * transition is brief and best-effort — see RemoteBinding). Firing the
 * callback while the AP isn't Active yet (e.g. still Activating) is a
 * harmless no-op — RemoteApController::deactivate() only acts when Active.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{
    enum class WirelessSetupPhase : uint8_t
    {
        Idle,       /* Not yet activated — shown identically to Connecting
                     * (see WirelessSetupScreen::set_view_model()), since
                     * main.cpp activates on open() and this phase is only
                     * ever momentarily true, if at all. */
        Activating, /* Waiting on the C3 (also covers deactivating->idle). */
        Active      /* AP is up; qr_payload/url_text are valid. */
    };

    constexpr uint8_t WIRELESS_QR_PAYLOAD_LEN = 128;
    constexpr uint8_t WIRELESS_SSID_LEN       = 33;
    constexpr uint8_t WIRELESS_URL_LEN        = 40;

    struct WirelessSetupViewModel
    {
            WirelessSetupPhase phase = WirelessSetupPhase::Idle;

            /** "http://<ip>" — opens the config site, same value as
             * url_text below. Not a Wi-Fi-join QR: the AP is deliberately
             * open (see RemoteApController's file doc comment), so joining
             * is just picking the SSID by hand; scanning this QR is the
             * second step, once already on the network. Fed straight to
             * lv_qrcode_update(). Valid only when phase==Active. */
            char qr_payload[WIRELESS_QR_PAYLOAD_LEN] = "";

            /** Shown as text underneath the QR code — a scan fallback for
             * anyone who'd rather type it in by hand. Valid only when
             * phase==Active. */
            char ssid[WIRELESS_SSID_LEN] = "";

            /** "http://<ip>" — for anyone who's already joined the
             * network. Valid only when phase==Active. */
            char url_text[WIRELESS_URL_LEN] = "";
    };

    class WirelessSetupScreen
    {
        public:
            using DeactivateCallback = void (*)(void *ctx);

            void init(const Theme       &theme,
                      DeactivateCallback deactivate_cb,
                      void              *deactivate_ctx);

            /** Opens the screen showing `vm`'s current phase. Callers
             * activate the AP (see gui::RemoteBinding::activate()) before
             * calling this, not this screen — see the file doc comment. */
            void open(const WirelessSetupViewModel &vm,
                      lv_obj_t                     *return_screen);

            /** Re-pulls the current phase/QR payload without changing
             * screens — called periodically (see main.cpp) so the
             * Activating -> Active transition (an async round trip to the
             * C3) shows up while this screen is on-screen, and so an idle
             * timeout auto-deactivation is reflected if the user is still
             * looking at it. A no-op visual refresh when nothing changed
             * (set_view_model() below only touches widgets that actually
             * need it). */
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
            /** Spinner + status text, shown for both Idle and Activating
             * — see WirelessSetupPhase::Idle's doc comment above. */
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
