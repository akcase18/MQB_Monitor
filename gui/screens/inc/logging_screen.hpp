/**
 * @file logging_screen.hpp
 * @brief The signal-log recorder's own screen: one big on/off toggle, a
 * short status readout (captures saved, where they're going, how much
 * room there is, how much has been recorded), and the erase action that
 * reclaims the store once logs have been offloaded.
 *
 * Reached by swiping *up* from InfoScreen, mirroring the swipe-down that
 * opens Settings — logging deliberately doesn't live under Settings. It's
 * something the user flips mid-drive when something interesting starts
 * happening, and three taps deep behind a settings menu is too far for
 * that. The gesture pair also reads as a natural opposition: pull
 * configuration down, pull the recorder up.
 *
 * Like every other gui/screens screen this is driven entirely by a POD
 * view-model and has zero core::/platform:: dependency — main.cpp builds
 * LoggingViewModel from core::Logger and the active IStorage. The toggle
 * fires ToggleCallback immediately rather than on close: it takes effect
 * at once and the button itself shows the result, so there's nothing
 * deferred for a Done step to commit. Erase works the same way, behind
 * the shared Cancel/Confirm popup (screen_helpers' create_confirm_popup)
 * that rotating the display already sits behind — the two changes on this
 * device that shouldn't happen on one stray press.
 *
 * The status readout is four caption-over-value tiles rather than four
 * caption-left/value-right rows. Stacking each pair lets the value use
 * the full cell width instead of width-minus-caption, which is what makes
 * the two-number "4.2 / 13.0 MB" fit; it also halves the vertical space
 * in Landscape, where the tiles sit 2x2 and the room that frees is what
 * the erase button occupies. Portrait keeps them in one column — at 320px
 * wide a 2x2 grid would put the usage figure back into a cell too narrow
 * for it — and has the height to spare anyway.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{

    constexpr uint8_t LOGGING_STORAGE_LABEL_LEN = 12;

    struct LoggingViewModel
    {
            /** Whether a capture is running right now. */
            bool recording = false;

            /** False if the log store never mounted — the toggle is
             * pointless in that case and the screen says so rather than
             * letting the user flip a switch that can't record. */
            bool storage_ready = false;

            /** Captures still holding data in the ring. Falls as the ring
             * laps past old ones — see core/logging's LogIndexEntry. */
            uint16_t log_count = 0;

            /** Summed length of those captures, already converted to
             * whole minutes and deliberately *not* rolled over into
             * hours (a plain "142 min" is easier to compare at a glance
             * than "2:22" when what's being judged is how much recording
             * is banked up). */
            uint32_t total_logged_min = 0;

            /** "Flash", "SD Card", … — where logs are actually going.
             * A string rather than an enum so gui/ needs no knowledge of
             * platform::StorageMedium. */
            char storage_label[LOGGING_STORAGE_LABEL_LEN] = "";

            /** What the log ring itself can hold — the figure that
             * actually bounds recording. Fixed by
             * CHUNK_COUNT * CHUNK_SIZE_BYTES, so it does not yet grow
             * with a roomier medium (see TODO.md). */
            uint64_t ring_capacity_bytes = 0;

            /** How much of that ring currently holds data. Shown as
             * used/total rather than as space remaining — the ring
             * overwrites instead of filling up, so once it has lapped
             * this pins at capacity and stays there while recording
             * carries on. */
            uint64_t used_bytes = 0;
    };

    class LoggingScreen
    {
        public:
            /** Fired the moment the toggle is tapped, with its new state.
             * Persisted by the caller right away — see the file doc
             * comment. */
            using ToggleCallback = void (*)(void *ctx, bool enabled);

            /** Fired once the erase popup's Confirm is pressed, never on
             * the press of the Erase button itself. The caller does the
             * actual wipe (core::Logger::erase()) and is expected to stop
             * recording first — see the popup's wording. */
            using EraseCallback = void (*)(void *ctx);

            void init(const Theme   &theme,
                      ToggleCallback toggle_cb,
                      void          *toggle_ctx,
                      EraseCallback  erase_cb,
                      void          *erase_ctx);

            /** Opens the screen showing `vm`. `return_screen` is what the
             * X button goes back to. */
            void open(const LoggingViewModel &vm, lv_obj_t *return_screen);

            /** Re-pulls the view-model without changing screens — called
             * periodically while this screen is up (see main.cpp) so the
             * recorded-minutes and capture-count readouts advance live
             * during a capture instead of freezing at whatever they were
             * when it opened. */
            void refresh(const LoggingViewModel &vm);

            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            static void close_clicked_cb(lv_event_t *e);
            static void toggle_clicked_cb(lv_event_t *e);
            static void erase_clicked_cb(lv_event_t *e);
            static void erase_confirm_cb(lv_event_t *e);
            static void erase_cancel_cb(lv_event_t *e);

            void set_view_model(const LoggingViewModel &vm);
            /** Repaints the toggle to match recording_/storage_ready_. */
            void refresh_toggle();
            /** Greys the erase button out when there's no store to wipe. */
            void refresh_erase();

            /* Stashed so refresh_toggle() (fired long after init()) can
             * reach the palette — same reason SettingsScreen keeps one. */
            const Theme *theme_ = nullptr;

            lv_obj_t *screen_        = nullptr;
            lv_obj_t *return_screen_ = nullptr;

            lv_obj_t *toggle_btn_   = nullptr;
            lv_obj_t *toggle_lbl_   = nullptr;
            lv_obj_t *toggle_hint_  = nullptr;

            lv_obj_t *erase_btn_ = nullptr;
            lv_obj_t *erase_lbl_ = nullptr;
            /* Live only while the confirm popup is up; both its handlers
             * close it and clear this, the same lifetime SettingsScreen's
             * orientation popup has. */
            lv_obj_t *erase_confirm_mbox_ = nullptr;

            /* Value halves of the four status tiles; their captions
             * never change so they aren't kept. */
            lv_obj_t *count_value_    = nullptr;
            lv_obj_t *storage_value_  = nullptr;
            lv_obj_t *capacity_value_ = nullptr;
            lv_obj_t *recorded_value_ = nullptr;

            bool recording_     = false;
            bool storage_ready_ = false;

            ToggleCallback toggle_cb_  = nullptr;
            void          *toggle_ctx_ = nullptr;
            EraseCallback  erase_cb_   = nullptr;
            void          *erase_ctx_  = nullptr;
    };

} // namespace gui
