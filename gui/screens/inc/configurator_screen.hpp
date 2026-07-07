/**
 * @file configurator_screen.hpp
 * @brief Panel reassignment: pick a different signal and unit system for
 * whichever panel opened this screen, or jump to WarningConfigScreen for
 * that signal's thresholds. Ported from the old Configurator, with the
 * runtime Info* list replaced by a caller-supplied signal name/id table
 * (gui/screens has no core:: — gui/bindings supplies that table from
 * core::SIGNAL_CATALOG).
 *
 * Layout follows WarningConfigScreen/SettingsScreen's pattern: a top nav
 * bar (title + X close) over stacked sections separated by full-width
 * dividers — Signal, Units (a 2-segment Imperial/Metric matrix, replacing
 * the old two independent checkboxes), Display (a 3-segment Digital/Bar/
 * Gauge matrix), and Warning Thresholds. Signal and Warning Thresholds are
 * both tappable rows (label + chevron) that hand off to another screen
 * rather than embedding their picker inline — a roller embedded directly
 * here (the original design) came out far taller than expected (see
 * SignalPickerScreen's doc comment) and spilled into the section below
 * it; a full screen has all the room a roller needs without fighting the
 * rest of this screen's layout for space.
 */
#pragma once

#include <lvgl.h>

#include "panel_widget.hpp"
#include "theme.hpp"

namespace gui
{

    class ConfiguratorScreen
    {
        public:
            using DoneCallback = void (*)(void       *ctx,
                                          uint8_t     panel_idx,
                                          uint16_t    signal_id,
                                          bool        is_metric,
                                          DisplayMode mode);
            using SettingsCallback
                = void (*)(void *ctx, uint8_t panel_idx, uint16_t signal_id);
            /** Fired when the Signal row is tapped — `current_signal_id` is
             * the pending selection (not necessarily what the panel was
             * opened with, if the user already picked a different one and
             * came back). */
            using PickSignalCallback
                = void (*)(void *ctx, uint16_t current_signal_id);

            /**
             * @param signal_names Signal names, indexed by catalog position
             * (not signal id) — used to display the Signal row's current
             * value and to resolve SignalPickerScreen's chosen index back
             * to a signal id.
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
                      void                    *settings_ctx,
                      PickSignalCallback       pick_signal_cb,
                      void                    *pick_signal_ctx);

            /** Opens the screen for `panel_idx`, pre-selecting
             * `signal_id`/`is_metric`/`mode`. */
            void open(uint8_t     panel_idx,
                      uint16_t    signal_id,
                      bool        is_metric,
                      DisplayMode mode);

            /** Updates the pending signal selection and the Signal row's
             * displayed name — called after SignalPickerScreen returns,
             * before this screen is shown again. */
            void set_selected_signal(uint16_t signal_id);

            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            static void done_cb_handler(lv_event_t *e);
            static void settings_cb_handler(lv_event_t *e);
            static void pick_signal_cb_handler(lv_event_t *e);

            void refresh_signal_row_label();

            lv_obj_t *screen_           = nullptr;
            lv_obj_t *signal_value_lbl_ = nullptr;
            lv_obj_t *signal_chevron_   = nullptr;
            lv_obj_t *units_matrix_     = nullptr;
            lv_obj_t *mode_matrix_      = nullptr;

            const char *const *signal_names_ = nullptr;
            const uint16_t    *signal_ids_   = nullptr;
            uint16_t           num_signals_  = 0;

            uint8_t  current_panel_idx_ = 0;
            uint16_t opened_signal_id_  = 0;
            uint16_t pending_signal_id_ = 0;

            DoneCallback       done_cb_         = nullptr;
            void              *done_ctx_        = nullptr;
            SettingsCallback   settings_cb_     = nullptr;
            void              *settings_ctx_    = nullptr;
            PickSignalCallback pick_signal_cb_  = nullptr;
            void              *pick_signal_ctx_ = nullptr;
    };

} // namespace gui
