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

struct PanelViewModel
{
        char name[PANEL_NAME_LEN]       = "";
        char unit_label[PANEL_UNIT_LABEL_LEN] = "";
        char value_str[PANEL_VALUE_LEN] = "--";
        bool large                      = false;
        bool warn_lo_active             = false;
        bool warn_hi_active             = false;
};

class PanelWidget
{
    public:
        /** Creates the button/data/label objects under `parent`. */
        void init(lv_obj_t *parent, const Theme &theme);

        /** Applies a new view-model: text, size variant, warning highlight. */
        void set_view_model(const PanelViewModel &vm);

        lv_obj_t *button() const
        {
            return btn_;
        }

    private:
        lv_obj_t     *btn_   = nullptr;
        lv_obj_t     *data_  = nullptr;
        lv_obj_t     *label_ = nullptr;
        const Theme *theme_ = nullptr;
};

} // namespace gui
