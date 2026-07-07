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
            using ViewModelBuilder   = PanelViewModel (*)(void   *ctx,
                                                          uint8_t panel_idx,
                                                          bool    large,
                                                          bool    hero);
            using PanelPressCallback = void (*)(void *ctx, uint8_t panel_idx);

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

            /** Re-pulls view-models for all active panels — call periodically
             * to keep values "live". */
            void refresh();

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

            lv_obj_t    *screen_ = nullptr;
            const Theme *theme_  = nullptr;

            PanelWidget panels_[MAX_PANELS];
            bool        panel_large_[MAX_PANELS] = {};
            /** True for the truly roomy full-height slots — count 1's
             * panel and count 3's hero, not count 2's narrower columns —
             * derived from PanelSlot::rowspan/colspan in rebuild(). */
            bool     panel_hero_[MAX_PANELS] = {};
            ClickCtx click_ctx_[MAX_PANELS]  = {};
            uint8_t  num_panels_             = 0;

            lv_coord_t col_dsc_[MAX_PANELS + 1] = {};
            lv_coord_t row_dsc_[MAX_PANELS + 1] = {};

            ViewModelBuilder   builder_     = nullptr;
            void              *builder_ctx_ = nullptr;
            PanelPressCallback press_cb_    = nullptr;
            void              *press_ctx_   = nullptr;
    };

} // namespace gui
