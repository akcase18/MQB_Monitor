#include "signal_picker_screen.hpp"

#include <string.h>

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        constexpr size_t OPTIONS_BUF_LEN = 1024;

        constexpr lv_coord_t HEADER_DIVIDER_Y = 50;
        constexpr lv_coord_t ROLLER_Y         = 66;
    } // namespace

    void SignalPickerScreen::init(const Theme             &theme,
                                  const char *const *const signal_names,
                                  const uint16_t          *signal_ids,
                                  uint16_t                 num_signals,
                                  DoneCallback             done_cb,
                                  void                    *ctx)
    {
        signal_ids_  = signal_ids;
        num_signals_ = num_signals;
        done_cb_     = done_cb;
        done_ctx_    = ctx;

        screen_ = create_screen(theme);

        lv_obj_t *title_lbl = lv_label_create(screen_);
        lv_label_set_text(title_lbl, "Select Signal");
        lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 20, 12);
        lv_obj_set_style_text_color(title_lbl, theme.palette.fg, 0);
        lv_obj_set_style_text_font(title_lbl, theme.typography.title, 0);

        DoneButton close = create_close_button(screen_, theme);
        lv_obj_add_event_cb(close.btn, done_cb_handler, LV_EVENT_CLICKED, this);

        create_divider(screen_, theme, HEADER_DIVIDER_Y);

        roller_ = lv_roller_create(screen_);
        lv_obj_set_style_text_font(roller_, theme.typography.ui, 0);
        lv_obj_set_width(roller_, theme.metrics.screen_width - 40);
        lv_obj_align(roller_, LV_ALIGN_TOP_MID, 0, ROLLER_Y);
        /* An even row count reads more clearly as "a scrollable strip"
         * than an odd one — with an odd count the selected row falls
         * exactly in the visual center, which can look like a fixed list
         * rather than something you scroll. No explicit set_height() here
         * — this screen (unlike the cramped inline roller this replaced,
         * see the file doc comment) has plenty of room, so just let this
         * size itself from the row count instead of fighting it with a
         * fixed height that ignores how many rows were asked for. */
        lv_roller_set_visible_row_count(roller_, 4);

        /* Default LVGL theming is a light-mode card — restyle to match
         * this app's dark, flat, bordered treatment. */
        lv_obj_set_style_bg_color(roller_, theme.palette.bg, 0);
        lv_obj_set_style_bg_opa(roller_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(roller_, theme.palette.fg, 0);
        lv_obj_set_style_border_width(
            roller_, theme.metrics.panel_border_width, 0);
        lv_obj_set_style_border_color(roller_, theme.palette.panel_border, 0);
        lv_obj_set_style_radius(roller_, 8, 0);
        lv_obj_set_style_shadow_width(roller_, 0, 0);
        lv_obj_set_style_bg_color(
            roller_, theme.palette.accent, LV_PART_SELECTED);
        lv_obj_set_style_bg_opa(roller_, LV_OPA_COVER, LV_PART_SELECTED);
        lv_obj_set_style_text_color(
            roller_, theme.palette.fg, LV_PART_SELECTED);

        char options[OPTIONS_BUF_LEN];
        options[0] = '\0';
        for (uint16_t i = 0; i < num_signals_; i++)
        {
            strncat(options,
                    signal_names[i],
                    sizeof(options) - strlen(options) - 1);
            if (i != num_signals_ - 1)
            {
                strncat(options, "\n", sizeof(options) - strlen(options) - 1);
            }
        }
        lv_roller_set_options(roller_, options, LV_ROLLER_MODE_NORMAL);
    }

    void SignalPickerScreen::open(uint16_t  current_signal_id,
                                  lv_obj_t *return_screen)
    {
        return_screen_ = return_screen;

        for (uint16_t i = 0; i < num_signals_; i++)
        {
            if (signal_ids_[i] == current_signal_id)
            {
                lv_roller_set_selected(roller_, i, LV_ANIM_OFF);
                break;
            }
        }

        lv_screen_load(screen_);
    }

    void SignalPickerScreen::done_cb_handler(lv_event_t *e)
    {
        auto *self
            = static_cast<SignalPickerScreen *>(lv_event_get_user_data(e));

        uint16_t selected
            = static_cast<uint16_t>(lv_roller_get_selected(self->roller_));
        uint16_t signal_id
            = (selected < self->num_signals_) ? self->signal_ids_[selected] : 0;

        if (self->done_cb_ != nullptr)
        {
            self->done_cb_(self->done_ctx_, signal_id);
        }

        if (self->return_screen_ != nullptr)
        {
            lv_screen_load(self->return_screen_);
        }
    }

} // namespace gui
