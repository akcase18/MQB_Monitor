/**
 * @file warning_config_screen.hpp
 * @brief Signal-scoped warning threshold editor. Ported from the old
 * WarningConfig, but actually wired to persistence this time — the old
 * screen's Done button never saved anything (see the plan's "known bugs"
 * list); this one calls DoneCallback with the final thresholds, and
 * gui/bindings::VehicleBinding::apply_warning_change persists them. Exit
 * is via an X close button in a top nav bar (not the bottom-right
 * checkmark gui/screens/screen_helpers.hpp's other screens use) — that
 * bar also holds the signal name, freeing the rest of the screen for the
 * two threshold rows without the close control colliding with them.
 *
 * Thresholds are edited/compared in whichever unit system the panel that
 * opened this screen currently displays the signal in (lo/hi/min_value/
 * max_value/step are all already converted) — VehicleBinding converts back
 * to the signal's native (metric) storage/comparison unit using
 * `is_metric` before persisting, so this screen itself never needs to know
 * about core::UnitType conversions.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{

    constexpr uint8_t WARNING_NAME_LEN       = 24;
    constexpr uint8_t WARNING_UNIT_LABEL_LEN = 10;

    struct WarningThresholdViewModel
    {
            bool  use_lo = false;
            bool  use_hi = false;
            float lo     = 0.0f;
            float hi     = 0.0f;

            /** Bounds/step for the +/- editor, from the signal's
             * SignalSpec::display_min/max and core::warning_step() —
             * already converted to whichever unit system `is_metric`
             * says, same as lo/hi. */
            float   min_value = 0.0f;
            float   max_value = 100.0f;
            float   step      = 1.0f;
            uint8_t decimals  = 0;

            /** Display-only context, shown on screen; round-tripped through
             * Done so apply_warning_change() knows which unit system to
             * convert lo/hi back from. */
            char name[WARNING_NAME_LEN]             = "";
            char unit_label[WARNING_UNIT_LABEL_LEN] = "";
            bool is_metric                          = true;
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
            /** Shared by all 4 +/- buttons — each one steps its own row's
             * value by +/-step, on both a short tap (CLICKED) and every
             * tick of a held long press (LONG_PRESSED_REPEAT). CLICKED
             * still fires after a long press releases, so `stepped_by_hold`
             * tells that trailing CLICKED not to double-step. */
            struct StepButtonCtx
            {
                    WarningConfigScreen *screen          = nullptr;
                    bool                 is_hi           = false;
                    float                direction       = 1.0f;
                    bool                 stepped_by_hold = false;
            };

            static void hi_toggle_cb(lv_event_t *e);
            static void lo_toggle_cb(lv_event_t *e);
            static void step_button_cb(lv_event_t *e);
            static void done_cb_handler(lv_event_t *e);

            static void set_enabled(lv_obj_t *obj, bool enabled);

            void step_value(bool is_hi, float direction);
            void refresh_value_labels();

            lv_obj_t *screen_        = nullptr;
            lv_obj_t *name_lbl_      = nullptr;
            lv_obj_t *close_btn_     = nullptr;
            lv_obj_t *hi_label_      = nullptr;
            lv_obj_t *hi_sel_        = nullptr;
            lv_obj_t *hi_value_lbl_  = nullptr;
            lv_obj_t *hi_minus_btn_  = nullptr;
            lv_obj_t *hi_plus_btn_   = nullptr;
            lv_obj_t *lo_label_      = nullptr;
            lv_obj_t *lo_sel_        = nullptr;
            lv_obj_t *lo_value_lbl_  = nullptr;
            lv_obj_t *lo_minus_btn_  = nullptr;
            lv_obj_t *lo_plus_btn_   = nullptr;
            lv_obj_t *return_screen_ = nullptr;

            StepButtonCtx hi_minus_ctx_ {};
            StepButtonCtx hi_plus_ctx_ {};
            StepButtonCtx lo_minus_ctx_ {};
            StepButtonCtx lo_plus_ctx_ {};

            uint16_t signal_id_ = 0;

            float   hi_value_  = 0.0f;
            float   lo_value_  = 0.0f;
            float   min_value_ = 0.0f;
            float   max_value_ = 100.0f;
            float   step_      = 1.0f;
            uint8_t decimals_  = 0;

            /** Carried from the opening WarningThresholdViewModel through
             * to refresh_value_labels() (unit suffix) and done_cb_handler()
             * (so apply_warning_change() knows which unit system to convert
             * lo/hi back from). */
            char unit_label_[WARNING_UNIT_LABEL_LEN] = "";
            bool is_metric_                          = true;

            DoneCallback done_cb_  = nullptr;
            void        *done_ctx_ = nullptr;
    };

} // namespace gui
