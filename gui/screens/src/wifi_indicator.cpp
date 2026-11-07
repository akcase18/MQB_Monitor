#include "wifi_indicator.hpp"
#include "mem_placement.hpp"

namespace gui
{

    namespace
    {
        /**
         * Inset from the corner. The top layer has no padding, so this
         * adds the screen margin to land where InfoScreen's recording LED
         * sits from its own corner.
         */
        constexpr lv_coord_t ICON_MARGIN = 8;
    } // namespace

    FLASHMEM void WifiIndicator::init(const Theme &theme)
    {
        if (icon_ != nullptr)
        {
            return;
        }

        lv_coord_t inset = ICON_MARGIN + theme.metrics.screen_margin;
        /**
         * The same grey as the screens' hint text.
         */
        idle_   = theme.palette.panel_border;
        joined_ = theme.palette.accent;

        icon_ = lv_label_create(lv_layer_top());
        lv_obj_set_style_text_font(icon_, theme.typography.symbol_sm, 0);
        lv_obj_set_style_text_color(icon_, idle_, 0);
        shows_joined_ = false;
        lv_label_set_text(icon_, LV_SYMBOL_WIFI);
        lv_obj_align(icon_, LV_ALIGN_BOTTOM_LEFT, inset, -inset);
        lv_obj_set_hidden(icon_, true);
    }

    FLASHMEM void WifiIndicator::destroy()
    {
        if (icon_ != nullptr)
        {
            lv_obj_delete(icon_);
            icon_ = nullptr;
        }
    }

    FLASHMEM void WifiIndicator::set_state(bool active, bool joined)
    {
        if (icon_ == nullptr)
        {
            return;
        }
        lv_obj_set_hidden(icon_, !active);
        if (joined != shows_joined_)
        {
            shows_joined_ = joined;
            lv_obj_set_style_text_color(icon_, joined ? joined_ : idle_, 0);
        }
    }

} // namespace gui
