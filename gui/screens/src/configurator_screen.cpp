#include "configurator_screen.hpp"

#include <stdio.h>
#include <string.h>

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        constexpr size_t OPTIONS_BUF_LEN = 1024;
    }

    void ConfiguratorScreen::init(const Theme             &theme,
                                  const char *const *const signal_names,
                                  const uint16_t          *signal_ids,
                                  uint16_t                 num_signals,
                                  DoneCallback             done_cb,
                                  void                    *done_ctx,
                                  SettingsCallback         settings_cb,
                                  void                    *settings_ctx)
    {
        signal_ids_   = signal_ids;
        num_signals_  = num_signals;
        done_cb_      = done_cb;
        done_ctx_     = done_ctx;
        settings_cb_  = settings_cb;
        settings_ctx_ = settings_ctx;

        screen_ = create_screen(theme);

        /* Signal selector */
        signal_roller_ = lv_roller_create(screen_);
        lv_obj_set_style_text_font(signal_roller_, theme.typography.ui, 0);
        lv_obj_set_width(signal_roller_,
                         (theme.metrics.screen_width / 3) * 2 - 20);
        lv_obj_align(signal_roller_, LV_ALIGN_TOP_LEFT, 20, 20);
        lv_roller_set_visible_row_count(signal_roller_, 4);

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
        lv_roller_set_options(signal_roller_, options, LV_ROLLER_MODE_INFINITE);

        /* Unit system radio buttons */
        radio_cont_ = lv_obj_create(screen_);
        lv_obj_set_scrollable(radio_cont_, false);
        lv_obj_set_size(
            radio_cont_, theme.metrics.screen_width / 3, LV_SIZE_CONTENT);
        lv_obj_align(radio_cont_, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_set_style_bg_color(radio_cont_, theme.palette.bg, 0);
        lv_obj_set_style_border_width(radio_cont_, 0, 0);
        lv_obj_set_flex_flow(radio_cont_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(radio_cont_,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(radio_cont_, 20, 0);
        lv_obj_set_style_pad_top(radio_cont_, 20, 0);
        lv_obj_set_style_pad_bottom(radio_cont_, 20, 0);

        radio_imperial_ = lv_checkbox_create(radio_cont_);
        lv_checkbox_set_text(radio_imperial_, "Imperial");
        lv_obj_set_style_text_color(radio_imperial_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(radio_imperial_, theme.typography.ui, 0);
        lv_obj_add_event_cb(radio_imperial_, radio_cb, LV_EVENT_CLICKED, this);

        radio_metric_ = lv_checkbox_create(radio_cont_);
        lv_checkbox_set_text(radio_metric_, "Metric");
        lv_obj_set_style_text_color(radio_metric_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(radio_metric_, theme.typography.ui, 0);
        lv_obj_add_event_cb(radio_metric_, radio_cb, LV_EVENT_CLICKED, this);

        /* Warning-threshold shortcut */
        settings_btn_ = lv_btn_create(screen_);
        lv_obj_set_style_bg_color(settings_btn_, theme.palette.bg, 0);
        lv_obj_set_style_text_color(settings_btn_, theme.palette.fg, 0);
        lv_obj_set_style_radius(settings_btn_, 5, 0);
        lv_obj_set_style_bg_color(
            settings_btn_, theme.palette.accent, LV_STATE_PRESSED);
        lv_obj_align(settings_btn_, LV_ALIGN_BOTTOM_LEFT, 20, -20);
        lv_obj_add_event_cb(
            settings_btn_, settings_cb_handler, LV_EVENT_CLICKED, this);

        lv_obj_t *settings_label = lv_label_create(settings_btn_);
        lv_obj_set_style_text_font(
            settings_label, theme.typography.symbol_lg, 0);
        lv_label_set_text(settings_label, LV_SYMBOL_SETTINGS);
        lv_obj_center(settings_label);

        /* Done */
        DoneButton done = create_done_button(screen_, theme);
        lv_obj_add_event_cb(done.btn, done_cb_handler, LV_EVENT_CLICKED, this);
    }

    void ConfiguratorScreen::open(uint8_t  panel_idx,
                                  uint16_t signal_id,
                                  bool     is_metric)
    {
        current_panel_idx_ = panel_idx;
        opened_signal_id_  = signal_id;

        for (uint16_t i = 0; i < num_signals_; i++)
        {
            if (signal_ids_[i] == signal_id)
            {
                lv_roller_set_selected(signal_roller_, i, LV_ANIM_OFF);
                break;
            }
        }

        if (is_metric)
        {
            lv_obj_add_state(radio_metric_, LV_STATE_CHECKED);
            lv_obj_remove_state(radio_imperial_, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_add_state(radio_imperial_, LV_STATE_CHECKED);
            lv_obj_remove_state(radio_metric_, LV_STATE_CHECKED);
        }

        lv_screen_load(screen_);
    }

    void ConfiguratorScreen::radio_cb(lv_event_t *e)
    {
        auto *self
            = static_cast<ConfiguratorScreen *>(lv_event_get_user_data(e));
        lv_obj_t *target = lv_event_get_target_obj(e);

        if (target == self->radio_imperial_)
        {
            lv_obj_add_state(self->radio_imperial_, LV_STATE_CHECKED);
            lv_obj_remove_state(self->radio_metric_, LV_STATE_CHECKED);
        }
        else if (target == self->radio_metric_)
        {
            lv_obj_add_state(self->radio_metric_, LV_STATE_CHECKED);
            lv_obj_remove_state(self->radio_imperial_, LV_STATE_CHECKED);
        }
    }

    void ConfiguratorScreen::done_cb_handler(lv_event_t *e)
    {
        auto *self
            = static_cast<ConfiguratorScreen *>(lv_event_get_user_data(e));
        if (self->done_cb_ == nullptr)
        {
            return;
        }

        uint16_t selected = static_cast<uint16_t>(
            lv_roller_get_selected(self->signal_roller_));
        uint16_t signal_id
            = (selected < self->num_signals_) ? self->signal_ids_[selected] : 0;
        bool is_metric
            = lv_obj_has_state(self->radio_metric_, LV_STATE_CHECKED);

        self->done_cb_(
            self->done_ctx_, self->current_panel_idx_, signal_id, is_metric);
    }

    void ConfiguratorScreen::settings_cb_handler(lv_event_t *e)
    {
        auto *self
            = static_cast<ConfiguratorScreen *>(lv_event_get_user_data(e));
        if (self->settings_cb_ != nullptr)
        {
            self->settings_cb_(self->settings_ctx_,
                               self->current_panel_idx_,
                               self->opened_signal_id_);
        }
    }

} // namespace gui
