#include "info_screen.hpp"
#include "mem_placement.hpp"

#include "screen_helpers.hpp"

namespace gui
{

    FLASHMEM void InfoScreen::init(const Theme       &theme,
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
        lv_obj_set_style_pad_all(screen_, theme.metrics.screen_margin, 0);
        lv_obj_set_style_pad_gap(screen_, theme.metrics.panel_gap, 0);
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

        /* LV_OBJ_FLAG_IGNORE_LAYOUT excludes this from screen_'s grid
         * layout (see rebuild() below) -- without it, the grid would try
         * to place this as a normal cell child and collide with panel 0.
         * Hidden until set_logging_active(true) — muted (grey, not fg
         * white) so it reads as a passive status glyph, not something
         * needing attention. */
        logging_icon_ = lv_label_create(screen_);
        lv_obj_add_flag(logging_icon_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_label_set_text(logging_icon_, LV_SYMBOL_SD_CARD);
        lv_obj_set_style_text_font(logging_icon_, theme.typography.icon_status, 0);
        lv_obj_set_style_text_color(
            logging_icon_, theme.palette.panel_border, 0);
        lv_obj_align(logging_icon_, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_add_flag(logging_icon_, LV_OBJ_FLAG_HIDDEN);
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

        uint8_t rank_of_slot[MAX_PANELS];
        compute_prominence_ranks(slots, count, rank_of_slot);

        bool horizontal = orientation == LayoutOrientation::Horizontal;

        /* Vertical's 5-panel layout (see layout.cpp) is a plain 2-col x
         * 3-row grid, not the fine-6-unit-axis/2-band-split scheme every
         * other count uses -- its grid descriptors are set up directly
         * rather than through band_dsc/split_dsc below. */
        if (count == 5 && orientation == LayoutOrientation::Vertical)
        {
            col_dsc_[0] = LV_GRID_FR(1);
            col_dsc_[1] = LV_GRID_FR(1);
            col_dsc_[2] = LV_GRID_TEMPLATE_LAST;

            row_dsc_[0] = LV_GRID_FR(1);
            row_dsc_[1] = LV_GRID_FR(1);
            row_dsc_[2] = LV_GRID_FR(1);
            row_dsc_[3] = LV_GRID_TEMPLATE_LAST;
        }
        else
        {
            /* The fine 6-unit axis always has 6 divisions and the
             * band-split axis always has 2 -- transpose() (see
             * layout.cpp) swaps which physical axis (row vs col) carries
             * which range, not the ranges themselves, so band_axis_len
             * must stay BAND_UNITS regardless of orientation. */
            uint8_t band_axis_len = BAND_UNITS;

            lv_coord_t *band_dsc  = horizontal ? col_dsc_ : row_dsc_;
            lv_coord_t *split_dsc = horizontal ? row_dsc_ : col_dsc_;

            for (uint8_t i = 0; i < band_axis_len; i++)
            {
                band_dsc[i] = LV_GRID_FR(1);
            }
            band_dsc[band_axis_len] = LV_GRID_TEMPLATE_LAST;

            /* Only the fixed Horizontal 5-panel layout keeps the
             * small/large band split — every other count either has no
             * meaningful row split (1/2, both rows fully spanned) or
             * wants equal-sized cells (3/4/6), so it splits 50/50
             * instead of pulling the theme's legacy sm/lg fractions. */
            float sm_pct = 0.5f;
            float lg_pct = 0.5f;
            if (count == 5)
            {
                sm_pct = theme_->metrics.panel_height_sm_pct;
                lg_pct = theme_->metrics.panel_height_lg_pct;
            }

            split_dsc[0] = LV_GRID_FR(static_cast<int32_t>(sm_pct * 100.0f));
            split_dsc[1] = LV_GRID_FR(static_cast<int32_t>(lg_pct * 100.0f));
            split_dsc[2] = LV_GRID_TEMPLATE_LAST;
        }

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
            panel_hero_[i]  = is_hero_slot(slot);

            rank_of_slot_[i]    = rank_of_slot[i];
            click_ctx_[i].index = rank_of_slot[i];
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
            PanelViewModel vm = builder_(builder_ctx_,
                                         rank_of_slot_[i],
                                         panel_large_[i],
                                         panel_hero_[i]);
            panels_[i].set_view_model(vm);
        }
    }

    void InfoScreen::set_logging_active(bool active)
    {
        if (active)
        {
            lv_obj_remove_flag(logging_icon_, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(logging_icon_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void InfoScreen::panel_clicked_cb(lv_event_t *e)
    {
        /* LVGL fires CLICKED on release whenever no ancestor actually
         * scrolled, even if the press traveled far enough to also fire a
         * GESTURE (this screen doesn't scroll, so a swipe-to-open-Settings
         * that starts on a panel would otherwise open that panel too).
         * gesture_dir stays set for the rest of this press once a gesture
         * fires, so this reliably tells a real tap from a swipe that
         * happened to start on a panel. */
        if (lv_indev_get_gesture_dir(lv_indev_active()) != LV_DIR_NONE)
        {
            return;
        }

        auto *ctx = static_cast<ClickCtx *>(lv_event_get_user_data(e));
        if (ctx->screen->press_cb_ != nullptr)
        {
            ctx->screen->press_cb_(ctx->screen->press_ctx_, ctx->index);
        }
    }

} // namespace gui
