#include "info_screen.hpp"
#include "mem_placement.hpp"

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        /* Small enough to sit unobtrusively over the panel grid's top-
         * right corner without covering a reading, large enough to catch
         * the eye when it blinks. */
        constexpr lv_coord_t LOGGING_LED_SIZE   = 12;
        /* Inset from the top-right corner, applied on both axes (so it
         * moves the LED left and down together). Sits on top of
         * screen_'s own small pad_all, so the visible gap is a little
         * larger than this alone. */
        constexpr lv_coord_t LOGGING_LED_MARGIN = 8;
        /* Half-period: lit this long, dark this long. Slow enough to
         * read as a deliberate recording light rather than an alarm. */
        constexpr uint32_t LOGGING_LED_BLINK_MS = 700;
    } // namespace

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

        /* Capture indicator: a small blinking red LED, the convention
         * every recording device uses, rather than the static SD-card
         * glyph this started as -- that one sat muted grey over a dense
         * panel grid and was easy to miss entirely, which is the one
         * thing a "you are recording" cue must not be.
         *
         * LV_OBJ_FLAG_IGNORE_LAYOUT excludes it from screen_'s grid
         * layout (see rebuild() below) -- without it, the grid would try
         * to place this as a normal cell child and collide with panel 0.
         * Hidden until set_logging_active(true). */
        logging_led_ = lv_led_create(screen_);
        lv_obj_add_flag(logging_led_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(logging_led_, LOGGING_LED_SIZE, LOGGING_LED_SIZE);
        lv_led_set_color(logging_led_, theme.palette.rec_indicator);
        lv_obj_align(logging_led_,
                     LV_ALIGN_TOP_RIGHT,
                     -LOGGING_LED_MARGIN,
                     LOGGING_LED_MARGIN);
        lv_obj_add_flag(logging_led_, LV_OBJ_FLAG_HIDDEN);
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
            lv_obj_remove_flag(logging_led_, LV_OBJ_FLAG_HIDDEN);

            /* A hard on/off blink, not a fade: lv_anim_path_step holds
             * the start value for the whole duration then jumps, so the
             * LED sits lit for BLINK_PERIOD_MS and dark for the same,
             * forever. Driven by an animation rather than an lv_timer so
             * its lifetime is tied to the object -- lv_obj_delete()
             * calls lv_anim_delete() on its target, so a screen rebuild
             * (see main.cpp's destroy_screens()) can't leave a timer
             * pointing at freed memory. */
            lv_anim_t anim;
            lv_anim_init(&anim);
            lv_anim_set_var(&anim, logging_led_);
            lv_anim_set_exec_cb(
                &anim,
                reinterpret_cast<lv_anim_exec_xcb_t>(lv_led_set_brightness));
            lv_anim_set_values(&anim, LV_LED_BRIGHT_MAX, LV_LED_BRIGHT_MIN);
            lv_anim_set_duration(&anim, LOGGING_LED_BLINK_MS);
            lv_anim_set_reverse_duration(&anim, LOGGING_LED_BLINK_MS);
            lv_anim_set_path_cb(&anim, lv_anim_path_step);
            lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&anim);
        }
        else
        {
            /* Stop the blink before hiding, so re-enabling starts a
             * fresh animation rather than stacking a second one. */
            lv_anim_delete(logging_led_, nullptr);
            lv_obj_add_flag(logging_led_, LV_OBJ_FLAG_HIDDEN);
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
