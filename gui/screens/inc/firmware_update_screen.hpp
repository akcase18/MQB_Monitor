/**
 * @file firmware_update_screen.hpp
 * @brief Update mode (see platform/dash_s3/OTA_PLAN.md): shown once
 * someone has agreed to it on the panel. Waits for the phone to send the
 * firmware, shows a progress bar while it's received, then an "Install
 * and restart?" popup once it has been verified. Nothing is installed
 * until someone in the car taps Install. The close button, which leaves update
 * mode, is only there while waiting: mid-upload there's no clean way out, and
 * at Ready the popup's Cancel is the way out.
 *
 * Driven by FirmwareUpdateViewModel (built by gui::RemoteBinding from
 * remote::server::RemoteOtaController), like every gui/screens screen. Like
 * SnifferScreen it only exists while in use: the composition root create()s it
 * on entering update mode and destroy()s it on leaving.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{
    enum class FirmwareUpdatePhase : uint8_t
    {
        Waiting,    /* update mode on, no upload yet */
        Receiving,  /* percent is valid */
        Ready,      /* verified */
        Installing, /* Install tapped */
    };

    constexpr uint8_t FIRMWARE_VERSION_TEXT_LEN = 32;

    struct FirmwareUpdateViewModel
    {
            FirmwareUpdatePhase phase   = FirmwareUpdatePhase::Waiting;
            uint8_t             percent = 0;
            /**
             * Waiting only: the last upload (or install) failed, so the
             * phone should send the file again.
             */
            bool upload_failed = false;
            /**
             * The incoming firmware's version.
             */
            char version[FIRMWARE_VERSION_TEXT_LEN] = "";
    };

    class FirmwareUpdateScreen
    {
        public:
            /**
             * Fired on the popup's Install. The caller commits the
             * update; this screen then shows Installing.
             */
            using InstallCallback = void (*)(void *ctx);
            /**
             * Fired on the X (while waiting) and on the popup's Cancel.
             * The caller leaves update mode and loads whatever screen
             * comes next.
             */
            using LeaveCallback = void (*)(void *ctx);

            void create(const Theme    &theme,
                        InstallCallback install_cb,
                        void           *install_ctx,
                        LeaveCallback   leave_cb,
                        void           *leave_ctx);

            /**
             * Deletes the screen (deferred, so it's safe from the
             * screen's own X) and closes the Install popup if it's up.
             * Load another screen first if this one is showing. No-op if
             * not created.
             */
            void destroy();

            /**
             * Loads the screen. Only once created.
             */
            void open(const FirmwareUpdateViewModel &vm);

            /**
             * Re-pulls progress and phase without changing screens. This is
             * called periodically while an update is under way. Opens the
             * popup on reaching Ready, and closes it if the phase moves
             * on without it (a new upload superseding this one).
             */
            void refresh(const FirmwareUpdateViewModel &vm);

            /**
             * Null when not created.
             */
            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            static void install_clicked_cb(lv_event_t *e);
            static void leave_clicked_cb(lv_event_t *e);

            void set_view_model(const FirmwareUpdateViewModel &vm);
            void close_popup();

            /**
             * Stashed so set_view_model() can style the popup it opens
             * long after init().
             */
            const Theme *theme_ = nullptr;

            lv_obj_t  *screen_          = nullptr;
            lv_obj_t  *close_btn_       = nullptr;
            lv_obj_t  *status_lbl_      = nullptr;
            lv_obj_t  *bar_             = nullptr;
            lv_obj_t  *percent_lbl_     = nullptr;
            lv_obj_t  *hint_lbl_        = nullptr;
            lv_coord_t hint_y_progress_ = 0;
            lv_coord_t hint_y_waiting_  = 0;
            lv_obj_t  *popup_           = nullptr;

            FirmwareUpdatePhase last_phase_   = FirmwareUpdatePhase::Waiting;
            uint8_t             last_percent_ = 0xFF;
            bool                last_failed_  = false;

            InstallCallback install_cb_  = nullptr;
            void           *install_ctx_ = nullptr;
            LeaveCallback   leave_cb_    = nullptr;
            void           *leave_ctx_   = nullptr;
    };

} // namespace gui
