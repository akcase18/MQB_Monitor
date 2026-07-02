/**
 * @file configurator_screen.hpp
 * @brief Panel reassignment: pick a different signal and unit system for
 * whichever panel opened this screen, or jump to WarningConfigScreen for
 * that signal's thresholds. Ported from the old Configurator, with the
 * runtime Info* list replaced by a caller-supplied signal name/id table
 * (gui/screens has no core:: — gui/bindings supplies that table from
 * core::SIGNAL_CATALOG). Uses a roller (not a dropdown) for signal
 * selection — much more forgiving on a resistive touchscreen than a
 * dense scrollable list.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{

    class ConfiguratorScreen
    {
        public:
            using DoneCallback = void (*)(void    *ctx,
                                          uint8_t  panel_idx,
                                          uint16_t signal_id,
                                          bool     is_metric);
            using SettingsCallback
                = void (*)(void *ctx, uint8_t panel_idx, uint16_t signal_id);

            /**
             * @param signal_names Signal names, indexed by catalog position
             * (not signal id) — populates the signal-selector roller.
             * @param signal_ids   Parallel array of catalog signal ids.
             * @param num_signals  Length of both arrays.
             */
            void init(const Theme             &theme,
                      const char *const *const signal_names,
                      const uint16_t          *signal_ids,
                      uint16_t                 num_signals,
                      DoneCallback             done_cb,
                      void                    *done_ctx,
                      SettingsCallback         settings_cb,
                      void                    *settings_ctx);

            /** Opens the screen for `panel_idx`, pre-selecting
             * `signal_id`/`is_metric`. */
            void open(uint8_t panel_idx, uint16_t signal_id, bool is_metric);

            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            static void radio_cb(lv_event_t *e);
            static void done_cb_handler(lv_event_t *e);
            static void settings_cb_handler(lv_event_t *e);

            lv_obj_t *screen_         = nullptr;
            lv_obj_t *signal_roller_  = nullptr;
            lv_obj_t *radio_cont_     = nullptr;
            lv_obj_t *radio_imperial_ = nullptr;
            lv_obj_t *radio_metric_   = nullptr;
            lv_obj_t *settings_btn_   = nullptr;

            const uint16_t *signal_ids_  = nullptr;
            uint16_t        num_signals_ = 0;

            uint8_t  current_panel_idx_ = 0;
            uint16_t opened_signal_id_  = 0;

            DoneCallback     done_cb_      = nullptr;
            void            *done_ctx_     = nullptr;
            SettingsCallback settings_cb_  = nullptr;
            void            *settings_ctx_ = nullptr;
    };

} // namespace gui
