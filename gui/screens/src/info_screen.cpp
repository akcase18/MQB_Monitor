#include "info_screen.hpp"

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        constexpr uint8_t BAND_UNITS = 6;
    } // namespace

    void InfoScreen::init(const Theme       &theme,
                          ViewModelBuilder   builder,
                          void              *builder_ctx,
                          PanelPressCallback press_cb,
                          void              *press_ctx)
    {
        theme_       = &theme;
        builder_     = builder;
        builder_ctx_ = builder_ctx;
        press_cb_    = press_cb;
        press_ctx_   = press_ctx;

        screen_ = create_screen(theme);
        lv_obj_set_style_pad_all(screen_, 0, 0);
        lv_obj_set_style_pad_gap(screen_, 0, 0);
        lv_obj_set_scrollbar_mode(screen_, LV_SCROLLBAR_MODE_OFF);

        for (uint8_t i = 0; i < MAX_PANELS; i++)
        {
            panels_[i].init(screen_, theme);
            panels_[i].set_visible(false);

            click_ctx_[i] = { this, i };
            lv_obj_add_event_cb(panels_[i].button(),
                                panel_clicked_cb,
                                LV_EVENT_CLICKED,
                                &click_ctx_[i]);
        }
    }

    void InfoScreen::rebuild(uint8_t count, LayoutOrientation orientation)
    {
        if (count < 1)
        {
            count = 1;
        }
        else if (count > MAX_PANELS)
        {
            count = MAX_PANELS;
        }

        PanelSlot slots[MAX_PANELS];
        compute_layout(count, orientation, slots);

        bool    horizontal    = orientation == LayoutOrientation::Horizontal;
        uint8_t band_axis_len = horizontal ? BAND_UNITS : 2;

        lv_coord_t *band_dsc  = horizontal ? col_dsc_ : row_dsc_;
        lv_coord_t *split_dsc = horizontal ? row_dsc_ : col_dsc_;

        for (uint8_t i = 0; i < band_axis_len; i++)
        {
            band_dsc[i] = LV_GRID_FR(1);
        }
        band_dsc[band_axis_len] = LV_GRID_TEMPLATE_LAST;

        split_dsc[0] = LV_GRID_FR(
            static_cast<int32_t>(theme_->metrics.panel_height_sm_pct * 100.0f));
        split_dsc[1] = LV_GRID_FR(
            static_cast<int32_t>(theme_->metrics.panel_height_lg_pct * 100.0f));
        split_dsc[2] = LV_GRID_TEMPLATE_LAST;

        lv_obj_set_grid_dsc_array(screen_, col_dsc_, row_dsc_);

        for (uint8_t i = 0; i < MAX_PANELS; i++)
        {
            bool active = i < count;
            panels_[i].set_visible(active);

            if (!active)
            {
                continue;
            }

            const PanelSlot &slot = slots[i];
            lv_obj_set_grid_cell(panels_[i].button(),
                                 LV_GRID_ALIGN_STRETCH,
                                 slot.col,
                                 slot.colspan,
                                 LV_GRID_ALIGN_STRETCH,
                                 slot.row,
                                 slot.rowspan);
            panel_large_[i] = slot.large;
        }

        num_panels_ = count;
        refresh();
    }

    void InfoScreen::refresh()
    {
        if (builder_ == nullptr)
        {
            return;
        }

        for (uint8_t i = 0; i < num_panels_; i++)
        {
            PanelViewModel vm = builder_(builder_ctx_, i, panel_large_[i]);
            panels_[i].set_view_model(vm);
        }
    }

    void InfoScreen::panel_clicked_cb(lv_event_t *e)
    {
        auto *ctx = static_cast<ClickCtx *>(lv_event_get_user_data(e));
        if (ctx->screen->press_cb_ != nullptr)
        {
            ctx->screen->press_cb_(ctx->screen->press_ctx_, ctx->index);
        }
    }

} // namespace gui
