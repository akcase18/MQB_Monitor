/**
 * @file warning_config_screen.hpp
 * @brief Signal-scoped warning threshold editor. Ported from the old
 * WarningConfig, but actually wired to persistence this time — the old
 * screen's Done button never saved anything (see the plan's "known bugs"
 * list); this one calls DoneCallback with the final thresholds, and
 * gui/bindings::VehicleBinding::apply_warning_change persists them.
 *
 * Thresholds are edited/compared in the signal's native (metric) storage
 * unit regardless of which unit system a panel currently displays it in
 * — simpler than converting on the fly, and arguably more correct than
 * the old behavior (which silently compared against whatever unit the
 * panel happened to be showing at the time).
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{

    struct WarningThresholdViewModel
    {
            bool  use_lo = false;
            bool  use_hi = false;
            float lo     = 0.0f;
            float hi     = 0.0f;
    };

    class WarningConfigScreen
    {
        public:
            using DoneCallback
                = void (*)(void                            *ctx,
                           uint16_t                         signal_id,
                           const WarningThresholdViewModel &result);

            void init(const Theme &theme, DoneCallback done_cb, void *ctx);

            /** Opens the screen for `signal_id`, pre-populated from `vm`;
             * returns to `return_screen` on Done. */
            void open(uint16_t                         signal_id,
                      const WarningThresholdViewModel &vm,
                      lv_obj_t                        *return_screen);

            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            static void master_toggle_cb(lv_event_t *e);
            static void hi_toggle_cb(lv_event_t *e);
            static void lo_toggle_cb(lv_event_t *e);
            static void done_cb_handler(lv_event_t *e);

            static void set_enabled(lv_obj_t *obj, bool enabled);

            lv_obj_t *screen_        = nullptr;
            lv_obj_t *master_sel_    = nullptr;
            lv_obj_t *hi_sel_        = nullptr;
            lv_obj_t *hi_slider_     = nullptr;
            lv_obj_t *lo_sel_        = nullptr;
            lv_obj_t *lo_slider_     = nullptr;
            lv_obj_t *return_screen_ = nullptr;

            uint16_t signal_id_ = 0;

            DoneCallback done_cb_  = nullptr;
            void        *done_ctx_ = nullptr;
    };

} // namespace gui
