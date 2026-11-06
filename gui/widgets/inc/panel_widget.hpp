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
            bool        warn_lo_active = false;
            bool        warn_hi_active = false;
            DisplayMode mode           = DisplayMode::Digital;
            /**
             * value_str is words (an Enum's label, or "?<n>"), not a
             * number -- sized by its length and by which glyphs the
             * data_* fonts have, not by the readout's worst case.
             */
            bool is_text = false;

            /**
             * Value's position within its signal's display range, 0..1.
             * Only meaningful for Bar/Gauge — ignored for Digital.
             */
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
             * flex, whatever) — this widget never sets its own
             * width/height, and sizes its contents (fonts, ring, bar) to
             * whatever box it's given, so it works the same whether
             * previewed standalone or placed by
             * gui/screens::compute_layout().
             */
            void init(lv_obj_t *parent, const Theme &theme);

            /**
             * Applies a new view-model: text, display mode, warning
             * highlight.
             */
            void set_view_model(const PanelViewModel &vm);

            /**
             * Shows/hides the panel without destroying it (for reflows where
             * the active panel count shrinks).
             */
            void set_visible(bool visible);

            lv_obj_t *button() const
            {
                return btn_;
            }

        private:
            /**
             * Picks the caption, value and unit fonts and sizes/places
             * the graphic, all from the panel's current size. Only on a
             * change of cell, mode, name or (for words) value.
             */
            void resize_contents(const PanelViewModel &vm, bool name_changed);

            /**
             * Centers data_ (and unit_ beside it) across the panel at
             * data_y_.
             */
            void place_value();

            lv_obj_t    *btn_   = nullptr;
            lv_obj_t    *arc_   = nullptr;
            lv_obj_t    *data_  = nullptr;
            lv_obj_t    *unit_  = nullptr;
            lv_obj_t    *bar_   = nullptr;
            lv_obj_t    *label_ = nullptr;
            const Theme *theme_ = nullptr;

            /**
             * The last view-model actually applied, so set_view_model()
             * can skip the LVGL work for whatever didn't change. On the
             * refresh path (every panel, several times a second) the
             * usual case is that only value_str moved -- name, unit,
             * fonts, mode, geometry and warning state all stay put for
             * minutes at a time -- yet every lv_label_set_text /
             * lv_obj_set_style_* / lv_obj_align call re-runs the style
             * cache invalidation and marks the panel's area dirty for
             * redraw whether or not the value it's setting differs.
             * `applied_valid_` starts false so the first call after
             * init() always writes everything.
             */
            PanelViewModel applied_ {};
            bool           applied_valid_ = false;

            /**
             * data_'s y in the content box, and unit_'s y relative to
             * it -- worked out with the fonts, kept for place_value().
             */
            lv_coord_t data_y_  = 0;
            lv_coord_t unit_dy_ = 0;
    };

} // namespace gui
