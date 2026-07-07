/**
 * @file panel_widget.hpp
 * @brief A single info-panel button (readout + name label), styled purely
 * from a Theme and driven purely by a POD PanelViewModel — no pointers
 * into core::vehicle/core::config, so it's previewable with fabricated
 * data independent of CAN/config wiring. gui/bindings (Phase 6) is the
 * only place allowed to build a PanelViewModel from real signal data.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{

    constexpr uint8_t PANEL_NAME_LEN       = 24;
    constexpr uint8_t PANEL_UNIT_LABEL_LEN = 10;
    constexpr uint8_t PANEL_VALUE_LEN      = 16;

    /**
     * How a panel presents its value. Digital is the bare tabular number;
     * Bar/Gauge add a fast-glance graphic alongside it — never instead of
     * it, since a graphic alone gives nothing at more than a glance.
     */
    enum class DisplayMode
    {
        Digital,
        Bar,
        Gauge
    };

    struct PanelViewModel
    {
            char name[PANEL_NAME_LEN]             = "";
            char unit_label[PANEL_UNIT_LABEL_LEN] = "";
            char value_str[PANEL_VALUE_LEN]       = "--";
            bool large                            = false;
            /** True only for the truly roomy full-height slots — count 1's
             * panel and count 3's hero (not count 2's narrower columns,
             * count 4, or count 5's large band) — so it gets its own
             * bigger font/arc tier instead of sharing `large`'s. */
            bool        hero           = false;
            bool        warn_lo_active = false;
            bool        warn_hi_active = false;
            DisplayMode mode           = DisplayMode::Digital;

            /** Value's position within its signal's display range, 0..1.
             * Only meaningful for Bar/Gauge — ignored for Digital. */
            float range_pct = 0.0f;
    };

    class PanelWidget
    {
        public:
            /**
             * Creates the button/data/label/bar/arc objects under `parent`.
             * All three display-mode visuals are pre-created up front and
             * shown/hidden per view-model rather than created on demand, so
             * changing a panel's mode (rare — only via the Configurator)
             * never allocates/frees LVGL objects on the hot refresh path.
             * Sizing is entirely the parent's responsibility (grid cell,
             * flex, whatever) — this widget only ever sets its own internal
             * content styling (fonts/colors/text), never its own
             * width/height, so it works the same whether previewed
             * standalone or placed by gui/screens::compute_layout().
             */
            void init(lv_obj_t *parent, const Theme &theme);

            /** Applies a new view-model: text, size variant, display mode,
             * warning highlight. */
            void set_view_model(const PanelViewModel &vm);

            /** Shows/hides the panel without destroying it (for reflows where
             * the active panel count shrinks). */
            void set_visible(bool visible);

            lv_obj_t *button() const
            {
                return btn_;
            }

        private:
            lv_obj_t    *btn_   = nullptr;
            lv_obj_t    *arc_   = nullptr;
            lv_obj_t    *data_  = nullptr;
            lv_obj_t    *unit_  = nullptr;
            lv_obj_t    *bar_   = nullptr;
            lv_obj_t    *label_ = nullptr;
            const Theme *theme_ = nullptr;
    };

} // namespace gui
