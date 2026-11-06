/**
 * @file info_screen.hpp
 * @brief The main dashboard screen: up to MAX_PANELS PanelWidgets placed
 * by compute_layout() and driven by view-models pulled through a
 * caller-supplied builder callback — InfoScreen itself never touches
 * core:: or gui/bindings, keeping it (like PanelWidget) previewable with
 * fabricated data.
 */
#pragma once

#include <lvgl.h>

#include "layout.hpp"
#include "panel_widget.hpp"
#include "theme.hpp"

namespace gui
{

    class InfoScreen
    {
        public:
            /**
             * `rank` is priority rank (0 = most important, see
             * core::Config::priorities[]) — not a screen slot index.
             */
            using ViewModelBuilder   = PanelViewModel (*)(void   *ctx,
                                                        uint8_t rank);
            using PanelPressCallback = void (*)(void *ctx, uint8_t rank);

            void init(const Theme       &theme,
                      ViewModelBuilder   builder,
                      void              *builder_ctx,
                      PanelPressCallback press_cb,
                      void              *press_ctx);

            /**
             * Repositions/resizes/shows panels for `count` (clamped 1-6) in
             * `orientation`, hides the rest, and immediately refreshes the
             * active ones. Call whenever Settings.num_panels/orientation
             * change.
             */
            void rebuild(uint8_t count, LayoutOrientation orientation);

            /**
             * Re-pulls view-models for all active panels — call periodically
             * to keep values "live".
             */
            void refresh();

            /**
             * Starts/stops the small blinking red capture LED in the
             * top-right corner — call whenever recording starts or stops
             * (see LoggingScreen's toggle, wired in main.cpp). InfoScreen
             * has no core:: dependency, so this takes a plain bool rather
             * than reading core::Logger itself. Safe to call repeatedly
             * with the same value: enabling restarts the blink from a
             * clean state rather than stacking animations.
             */
            void set_logging_active(bool active);

            /**
             * Shows/hides "Tap screen to exit preview" along the top,
             * for while a phone is previewing on the dashboard. The
             * panels move down to make room rather than sit under it --
             * every layout still fits (checked at both orientations).
             * Cheap to repeat with the same value.
             */
            void set_preview_banner(bool shown);

            lv_obj_t *screen() const
            {
                return screen_;
            }

        private:
            struct ClickCtx
            {
                    InfoScreen *screen;
                    uint8_t     index;
            };

            static void panel_clicked_cb(lv_event_t *e);

            lv_obj_t    *screen_        = nullptr;
            lv_obj_t    *logging_led_   = nullptr;
            lv_obj_t    *preview_lbl_   = nullptr;
            const Theme *theme_         = nullptr;

            PanelWidget panels_[MAX_PANELS];
            ClickCtx click_ctx_[MAX_PANELS]  = {};
            uint8_t  num_panels_             = 0;

            /**
             * rank_of_slot_[i] = which priority rank the widget in slot i
             * currently displays — recomputed every rebuild() via
             * compute_prominence_ranks(), consumed by refresh() and by
             * click_ctx_[i].index so a tap reports rank, not slot.
             */
            uint8_t rank_of_slot_[MAX_PANELS] = {};

            lv_coord_t col_dsc_[MAX_PANELS + 1] = {};
            lv_coord_t row_dsc_[MAX_PANELS + 1] = {};

            ViewModelBuilder   builder_     = nullptr;
            void              *builder_ctx_ = nullptr;
            PanelPressCallback press_cb_    = nullptr;
            void              *press_ctx_   = nullptr;
    };

} // namespace gui
