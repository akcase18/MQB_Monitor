#include "sniffer_screen.hpp"
#include "mem_placement.hpp"

#include <stdio.h>

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        /**
         * Nav bar, the four status tiles, then Stop button.
         */
        constexpr lv_coord_t HEADER_DIVIDER_Y = 50;

        constexpr lv_coord_t STAT_FIRST_Y    = 66;
        constexpr lv_coord_t STAT_HEIGHT     = 50;
        constexpr lv_coord_t STAT_STEP       = 64;
        constexpr uint8_t    STAT_COLUMNS    = 2;
        constexpr lv_coord_t STAT_COLUMN_GAP = 16;

        constexpr lv_coord_t STOP_DIVIDER_Y = 200;
        constexpr lv_coord_t STOP_ROW_Y     = 216;
        constexpr lv_coord_t STOP_HEIGHT    = 84;

        constexpr lv_coord_t PORTRAIT_STAT_FIRST_Y = 70;
        constexpr lv_coord_t PORTRAIT_STAT_STEP    = 62;

        constexpr lv_coord_t PORTRAIT_STOP_DIVIDER_Y = 336;
        constexpr lv_coord_t PORTRAIT_STOP_ROW_Y     = 352;
        constexpr lv_coord_t PORTRAIT_STOP_HEIGHT    = 104;

        constexpr lv_coord_t SCREEN_MARGIN = 20;

        constexpr uint8_t STATUS_TILE_COUNT = 4;

        const char *channel_text(SnifferChannel channel)
        {
            switch (channel)
            {
                case SnifferChannel::ListenOnly:
                    return "Listen-only";
                case SnifferChannel::Normal:
                    return "Normal";
                case SnifferChannel::Closed:
                default:
                    return "Waiting";
            }
        }
    } // namespace

    FLASHMEM void SnifferScreen::create(const Theme &theme,
                                        StopCallback stop_cb,
                                        void        *ctx,
                                        const char  *title)
    {
        theme_    = &theme;
        stop_cb_  = stop_cb;
        stop_ctx_ = ctx;

        screen_ = create_screen(theme);

        bool portrait
            = theme.metrics.screen_height > theme.metrics.screen_width;

        create_nav_title(screen_, theme, title);
        create_divider(screen_, theme, HEADER_DIVIDER_Y);

        lv_coord_t row_width = theme.metrics.screen_width - 2 * SCREEN_MARGIN;

        uint8_t    columns = portrait ? 1 : STAT_COLUMNS;
        lv_coord_t tile_width
            = portrait ? row_width
                       : static_cast<lv_coord_t>((row_width - STAT_COLUMN_GAP)
                                                 / STAT_COLUMNS);
        lv_coord_t column_step
            = static_cast<lv_coord_t>(tile_width + STAT_COLUMN_GAP);
        lv_coord_t first_y = portrait ? PORTRAIT_STAT_FIRST_Y : STAT_FIRST_Y;
        lv_coord_t step    = portrait ? PORTRAIT_STAT_STEP : STAT_STEP;

        const char *captions[STATUS_TILE_COUNT]
            = { "Channel", "Frames/s", "Total", "Dropped" };
        lv_obj_t **values[STATUS_TILE_COUNT]
            = { &channel_value_, &rate_value_, &total_value_, &dropped_value_ };

        for (uint8_t i = 0; i < STATUS_TILE_COUNT; i++)
        {
            uint8_t column = i % columns;
            uint8_t row    = i / columns;

            *values[i] = create_status_tile(
                screen_,
                theme,
                captions[i],
                static_cast<lv_coord_t>(SCREEN_MARGIN + column * column_step),
                static_cast<lv_coord_t>(first_y + row * step),
                tile_width,
                STAT_HEIGHT);
        }

        create_divider(screen_,
                       theme,
                       portrait ? PORTRAIT_STOP_DIVIDER_Y : STOP_DIVIDER_Y);

        lv_obj_t *stop_btn = lv_btn_create(screen_);
        lv_obj_set_size(
            stop_btn, row_width, portrait ? PORTRAIT_STOP_HEIGHT : STOP_HEIGHT);
        lv_obj_align(stop_btn,
                     LV_ALIGN_TOP_LEFT,
                     SCREEN_MARGIN,
                     portrait ? PORTRAIT_STOP_ROW_Y : STOP_ROW_Y);
        lv_obj_set_style_radius(stop_btn, 10, 0);
        lv_obj_set_style_bg_color(stop_btn, theme.palette.accent, 0);
        lv_obj_set_style_bg_opa(stop_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(
            stop_btn, theme.metrics.panel_border_width, 0);
        lv_obj_set_style_border_color(stop_btn, theme.palette.panel_border, 0);
        lv_obj_set_style_shadow_width(stop_btn, 0, 0);
        lv_obj_add_event_cb(stop_btn, stop_clicked_cb, LV_EVENT_CLICKED, this);

        lv_obj_t *stop_lbl = lv_label_create(stop_btn);
        lv_label_set_text(stop_lbl, "STOP");
        lv_obj_set_style_text_font(stop_lbl, theme.typography.title, 0);
        lv_obj_set_style_text_color(stop_lbl, theme.palette.fg, 0);
        lv_obj_align(stop_lbl, LV_ALIGN_CENTER, 0, -10);

        lv_obj_t *stop_hint = lv_label_create(stop_btn);
        lv_label_set_text(stop_hint, "back to gauges");
        lv_obj_set_style_text_font(stop_hint, theme.typography.label_sm, 0);
        lv_obj_set_style_text_color(stop_hint, theme.palette.fg, 0);
        lv_obj_align(stop_hint, LV_ALIGN_CENTER, 0, 14);

        refresh(SnifferViewModel {});
    }

    FLASHMEM void SnifferScreen::destroy()
    {
        if (screen_ == nullptr)
        {
            return;
        }

        lv_obj_delete(screen_);
        screen_        = nullptr;
        channel_value_ = nullptr;
        rate_value_    = nullptr;
        total_value_   = nullptr;
        dropped_value_ = nullptr;
    }

    FLASHMEM void SnifferScreen::refresh(const SnifferViewModel &vm)
    {
        if (screen_ == nullptr)
        {
            return;
        }

        lv_label_set_text(channel_value_,
                          vm.mode_text != nullptr ? vm.mode_text
                                                  : channel_text(vm.channel));

        char buf[16];

        snprintf(buf, sizeof(buf), "%lu", (unsigned long)vm.frames_per_sec);
        lv_label_set_text(rate_value_, buf);

        snprintf(buf, sizeof(buf), "%lu", (unsigned long)vm.total_frames);
        lv_label_set_text(total_value_, buf);

        snprintf(buf, sizeof(buf), "%lu", (unsigned long)vm.dropped_frames);
        lv_label_set_text(dropped_value_, buf);
        lv_obj_set_style_text_color(dropped_value_,
                                    vm.dropped_frames > 0
                                        ? theme_->palette.warn_hi
                                        : theme_->palette.fg,
                                    0);
    }

    FLASHMEM void SnifferScreen::stop_clicked_cb(lv_event_t *e)
    {
        SnifferScreen *self
            = static_cast<SnifferScreen *>(lv_event_get_user_data(e));

        if (self->stop_cb_ != nullptr)
        {
            self->stop_cb_(self->stop_ctx_);
        }
    }

} // namespace gui
