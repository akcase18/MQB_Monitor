/**
 * @file logging_screen.hpp
 * @brief The signal-log recorder's own screen: two big start/stop
 * buttons (record what's on screen, or record the log profile set from
 * the phone), a short status readout (captures saved, where they're going, how
 * much room there is, how much has been recorded), and the erase action that
 * reclaims the store once logs have been offloaded.
 *
 * Reached by swiping up from InfoScreen, mirroring the swipe-down that
 * opens Settings.
 *
 * Like every other gui/screens screen this is driven entirely by a POD
 * view-model and has zero core::/platform:: dependency. A start or
 * stop fires ToggleCallback immediately rather than on close. Erase works the
 * same way.
 *
 * The status readout is four caption-over-value tiles. In Landscape, the values
 * are arranged in a 2x2 grid. In Portrait, they are all in 1 column.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{

    constexpr uint8_t LOGGING_STORAGE_LABEL_LEN = 12;

    enum class LogSourceVm : uint8_t
    {
        Screen,
        Profile
    };

    struct LoggingViewModel
    {
            bool        recording = false;
            LogSourceVm source    = LogSourceVm::Screen;

            uint8_t screen_signals  = 0;
            uint8_t profile_signals = 0;

            bool storage_ready = false;

            /**
             * Captures still holding data in the ring. Falls as the ring
             * laps past old ones (see core/logging's LogIndexEntry).
             */
            uint16_t log_count = 0;

            /**
             * Summed length of those captures in minutes.
             */
            uint32_t total_logged_min = 0;

            /**
             * Where logs are actually going (flash, sd card, etc.).
             * A string rather than an enum so gui/ needs no knowledge of
             * platform::StorageMedium.
             */
            char storage_label[LOGGING_STORAGE_LABEL_LEN] = "";

            uint64_t ring_capacity_bytes = 0;
            uint64_t ring_used_bytes     = 0;
    };

    class LoggingScreen
    {
        public:
            using ToggleCallback
                = void (*)(void *ctx, bool enabled, LogSourceVm source);

            /**
             * Fired once the erase popup's Confirm is pressed.
             */
            using EraseCallback = void (*)(void *ctx);

            void init(const Theme   &theme,
                      ToggleCallback toggle_cb,
                      void          *toggle_ctx,
                      EraseCallback  erase_cb,
                      void          *erase_ctx);

            void open(const LoggingViewModel &vm, lv_obj_t *return_screen);

            /**
             * Re-pulls the view-model without changing screens.
             */
            void refresh(const LoggingViewModel &vm);

            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            static void close_clicked_cb(lv_event_t *e);
            static void button_clicked_cb(lv_event_t *e);
            static void erase_clicked_cb(lv_event_t *e);
            static void erase_confirm_cb(lv_event_t *e);
            static void erase_cancel_cb(lv_event_t *e);

            struct SourceButton
            {
                    LoggingScreen *owner  = nullptr;
                    LogSourceVm    source = LogSourceVm::Screen;
                    lv_obj_t      *btn    = nullptr;
                    lv_obj_t      *lbl    = nullptr;
                    lv_obj_t      *hint   = nullptr;
            };

            void create_source_button(SourceButton &b,
                                      LogSourceVm   source,
                                      lv_coord_t    x,
                                      lv_coord_t    y,
                                      lv_coord_t    width,
                                      lv_coord_t    height);

            void set_view_model(const LoggingViewModel &vm);

            void refresh_buttons();
            void refresh_button(SourceButton &b);

            void refresh_erase();

            const Theme *theme_ = nullptr;

            lv_obj_t *screen_        = nullptr;
            lv_obj_t *return_screen_ = nullptr;

            /**
             * [0] Screen, [1] Profile, left to right.
             */
            SourceButton buttons_[2];

            bool portrait_ = false;

            lv_obj_t *erase_btn_ = nullptr;
            lv_obj_t *erase_lbl_ = nullptr;

            lv_obj_t *erase_confirm_mbox_ = nullptr;

            lv_obj_t *count_value_    = nullptr;
            lv_obj_t *storage_value_  = nullptr;
            lv_obj_t *capacity_value_ = nullptr;
            lv_obj_t *recorded_value_ = nullptr;

            bool        recording_       = false;
            LogSourceVm source_          = LogSourceVm::Screen;
            bool        storage_ready_   = false;
            uint8_t     screen_signals_  = 0;
            uint8_t     profile_signals_ = 0;

            ToggleCallback toggle_cb_  = nullptr;
            void          *toggle_ctx_ = nullptr;
            EraseCallback  erase_cb_   = nullptr;
            void          *erase_ctx_  = nullptr;
    };

} // namespace gui
