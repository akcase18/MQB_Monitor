#include "logging_screen.hpp"
#include "mem_placement.hpp"

#include <stdio.h>

#include "screen_helpers.hpp"
#include "screen_transition.hpp"

namespace gui
{

    namespace
    {
        constexpr lv_coord_t HEADER_DIVIDER_Y = 50;

        constexpr lv_coord_t TOGGLE_ROW_Y     = 60;
        constexpr lv_coord_t TOGGLE_HEIGHT    = 58;
        constexpr lv_coord_t TOGGLE_DIVIDER_Y = 130;

        constexpr lv_coord_t STAT_FIRST_Y = 140;
        constexpr lv_coord_t STAT_HEIGHT  = 50;
        constexpr lv_coord_t STAT_STEP    = 54;
        /**
         * Landscape only. Portrait stacks all four in one column.
         */
        constexpr uint8_t    STAT_COLUMNS    = 2;
        constexpr lv_coord_t STAT_COLUMN_GAP = 16;

        constexpr lv_coord_t ERASE_DIVIDER_Y = 252;
        constexpr lv_coord_t ERASE_ROW_Y     = 260;
        constexpr lv_coord_t ERASE_HEIGHT    = 44;

        constexpr lv_coord_t PORTRAIT_TOGGLE_ROW_Y     = 66;
        constexpr lv_coord_t PORTRAIT_TOGGLE_HEIGHT    = 78;
        constexpr lv_coord_t PORTRAIT_TOGGLE_DIVIDER_Y = 158;

        constexpr lv_coord_t PORTRAIT_STAT_FIRST_Y = 172;
        constexpr lv_coord_t PORTRAIT_STAT_HEIGHT  = 50;
        constexpr lv_coord_t PORTRAIT_STAT_STEP    = 56;

        constexpr lv_coord_t PORTRAIT_ERASE_DIVIDER_Y = 402;
        constexpr lv_coord_t PORTRAIT_ERASE_ROW_Y     = 412;
        constexpr lv_coord_t PORTRAIT_ERASE_HEIGHT    = 52;

        constexpr lv_coord_t SCREEN_MARGIN = 20;
        constexpr lv_coord_t BUTTON_GAP    = 12;

        constexpr uint8_t STATUS_ROW_COUNT = 4;

        void format_usage(uint64_t used,
                          uint64_t total,
                          char    *out,
                          size_t   out_len)
        {
            constexpr uint64_t MB = 1024ull * 1024ull;
            constexpr uint64_t GB = MB * 1024ull;

            bool        gigabytes = total >= GB;
            uint64_t    unit      = gigabytes ? GB : MB;
            const char *suffix    = gigabytes ? "GB" : "MB";

            snprintf(out,
                     out_len,
                     "%.1f / %.1f %s",
                     static_cast<double>(used) / static_cast<double>(unit),
                     static_cast<double>(total) / static_cast<double>(unit),
                     suffix);
        }
    } // namespace

    FLASHMEM void LoggingScreen::init(const Theme   &theme,
                                      ToggleCallback toggle_cb,
                                      void          *toggle_ctx,
                                      EraseCallback  erase_cb,
                                      void          *erase_ctx)
    {
        theme_      = &theme;
        toggle_cb_  = toggle_cb;
        toggle_ctx_ = toggle_ctx;
        erase_cb_   = erase_cb;
        erase_ctx_  = erase_ctx;

        screen_ = create_screen(theme);

        bool portrait
            = theme.metrics.screen_height > theme.metrics.screen_width;

        create_nav_title(screen_, theme, "Logging");

        DoneButton close = create_close_button(screen_, theme);
        lv_obj_add_event_cb(
            close.btn, close_clicked_cb, LV_EVENT_CLICKED, this);

        create_divider(screen_, theme, HEADER_DIVIDER_Y);

        lv_coord_t row_width = theme.metrics.screen_width - 2 * SCREEN_MARGIN;

        portrait_ = portrait;
        lv_coord_t button_width
            = static_cast<lv_coord_t>((row_width - BUTTON_GAP) / 2);
        lv_coord_t button_y = portrait ? PORTRAIT_TOGGLE_ROW_Y : TOGGLE_ROW_Y;
        lv_coord_t button_height
            = portrait ? PORTRAIT_TOGGLE_HEIGHT : TOGGLE_HEIGHT;
        create_source_button(buttons_[0],
                             LogSourceVm::Screen,
                             SCREEN_MARGIN,
                             button_y,
                             button_width,
                             button_height);
        create_source_button(
            buttons_[1],
            LogSourceVm::Profile,
            static_cast<lv_coord_t>(SCREEN_MARGIN + button_width + BUTTON_GAP),
            button_y,
            button_width,
            button_height);

        create_divider(screen_,
                       theme,
                       portrait ? PORTRAIT_TOGGLE_DIVIDER_Y : TOGGLE_DIVIDER_Y);

        lv_coord_t first_y = portrait ? PORTRAIT_STAT_FIRST_Y : STAT_FIRST_Y;
        lv_coord_t step    = portrait ? PORTRAIT_STAT_STEP : STAT_STEP;
        lv_coord_t height  = portrait ? PORTRAIT_STAT_HEIGHT : STAT_HEIGHT;

        uint8_t    columns = portrait ? 1 : STAT_COLUMNS;
        lv_coord_t tile_width
            = portrait ? row_width
                       : static_cast<lv_coord_t>((row_width - STAT_COLUMN_GAP)
                                                 / STAT_COLUMNS);
        lv_coord_t column_step
            = static_cast<lv_coord_t>(tile_width + STAT_COLUMN_GAP);

        const char *captions[STATUS_ROW_COUNT]
            = { "Logs Saved", "Storage", "Used", "Recorded" };
        lv_obj_t **values[STATUS_ROW_COUNT] = {
            &count_value_, &storage_value_, &capacity_value_, &recorded_value_
        };

        for (uint8_t i = 0; i < STATUS_ROW_COUNT; i++)
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
                height);
        }

        create_divider(screen_,
                       theme,
                       portrait ? PORTRAIT_ERASE_DIVIDER_Y : ERASE_DIVIDER_Y);

        erase_btn_ = lv_btn_create(screen_);
        lv_obj_set_size(erase_btn_,
                        row_width,
                        portrait ? PORTRAIT_ERASE_HEIGHT : ERASE_HEIGHT);
        lv_obj_align(erase_btn_,
                     LV_ALIGN_TOP_LEFT,
                     SCREEN_MARGIN,
                     portrait ? PORTRAIT_ERASE_ROW_Y : ERASE_ROW_Y);
        lv_obj_set_style_radius(erase_btn_, 10, 0);
        lv_obj_set_style_bg_color(erase_btn_, theme.palette.bg, 0);
        lv_obj_set_style_bg_opa(erase_btn_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(
            erase_btn_, theme.metrics.panel_border_width, 0);
        lv_obj_set_style_shadow_width(erase_btn_, 0, 0);
        lv_obj_set_style_bg_color(
            erase_btn_, theme.palette.warn_hi, LV_STATE_PRESSED);
        lv_obj_add_event_cb(
            erase_btn_, erase_clicked_cb, LV_EVENT_CLICKED, this);

        erase_lbl_ = lv_label_create(erase_btn_);
        lv_label_set_text(erase_lbl_, "Erase Logs");
        lv_obj_set_style_text_font(erase_lbl_, theme.typography.ui, 0);
        lv_obj_center(erase_lbl_);

        refresh_buttons();
        refresh_erase();
    }

    FLASHMEM void LoggingScreen::create_source_button(SourceButton &b,
                                                      LogSourceVm   source,
                                                      lv_coord_t    x,
                                                      lv_coord_t    y,
                                                      lv_coord_t    width,
                                                      lv_coord_t    height)
    {
        const Theme &theme = *theme_;

        b.owner  = this;
        b.source = source;
        b.btn    = lv_btn_create(screen_);
        lv_obj_set_size(b.btn, width, height);
        lv_obj_align(b.btn, LV_ALIGN_TOP_LEFT, x, y);
        lv_obj_set_style_radius(b.btn, 10, 0);
        lv_obj_set_style_bg_opa(b.btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(
            b.btn, theme.metrics.panel_border_width, 0);
        lv_obj_set_style_border_color(b.btn, theme.palette.panel_border, 0);
        lv_obj_set_style_shadow_width(b.btn, 0, 0);
        lv_obj_set_style_pad_all(b.btn, 0, 0);
        lv_obj_add_event_cb(b.btn, button_clicked_cb, LV_EVENT_CLICKED, &b);

        b.lbl = lv_label_create(b.btn);
        lv_obj_set_style_text_font(b.lbl, theme.typography.title, 0);
        lv_obj_align(b.lbl, LV_ALIGN_CENTER, 0, -10);

        /**
         * Second line inside the button: how many signals it records, or
         * what a press will do next while recording.
         */
        b.hint = lv_label_create(b.btn);
        lv_obj_set_style_text_font(b.hint, theme.typography.label_sm, 0);
        lv_obj_align(b.hint, LV_ALIGN_CENTER, 0, 14);
    }

    void LoggingScreen::open(const LoggingViewModel &vm,
                             lv_obj_t               *return_screen)
    {
        return_screen_ = return_screen;
        set_view_model(vm);
        slide_screen_in(screen_, LV_SCR_LOAD_ANIM_OVER_TOP, 220);
    }

    void LoggingScreen::refresh(const LoggingViewModel &vm)
    {
        set_view_model(vm);
    }

    void LoggingScreen::set_view_model(const LoggingViewModel &vm)
    {
        recording_       = vm.recording;
        source_          = vm.source;
        storage_ready_   = vm.storage_ready;
        screen_signals_  = vm.screen_signals;
        profile_signals_ = vm.profile_signals;
        refresh_buttons();
        refresh_erase();

        char buf[32];

        snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(vm.log_count));
        lv_label_set_text(count_value_, buf);

        lv_label_set_text(storage_value_,
                          vm.storage_label[0] != '\0' ? vm.storage_label
                                                      : "--");

        /**
         * Used against the ring's total. Not "remaining": the ring
         * overwrites rather than filling up, so past the first lap this
         * pins at full and recording simply continues, dropping the
         * oldest chunk each time around. A countdown-to-zero would sit
         * at zero for the life of the device.
         */
        format_usage(
            vm.ring_used_bytes, vm.ring_capacity_bytes, buf, sizeof(buf));
        lv_label_set_text(capacity_value_, buf);

        snprintf(buf,
                 sizeof(buf),
                 "%u min",
                 static_cast<unsigned>(vm.total_logged_min));
        lv_label_set_text(recorded_value_, buf);
    }

    void LoggingScreen::refresh_buttons()
    {
        if (theme_ == nullptr)
        {
            return;
        }
        refresh_button(buttons_[0]);
        refresh_button(buttons_[1]);
    }

    void LoggingScreen::refresh_button(SourceButton &b)
    {
        const Palette &pal     = theme_->palette;
        bool           profile = b.source == LogSourceVm::Profile;
        uint8_t        signals = profile ? profile_signals_ : screen_signals_;
        const char    *name    = profile ? "PROFILE" : "SCREEN";

        char hint[24];
        snprintf(hint,
                 sizeof(hint),
                 signals == 1 ? "%u signal" : "%u signals",
                 static_cast<unsigned>(signals));

        bool active = recording_ && source_ == b.source;
        /**
         * Dimmed: drawn, so the screen still explains its own shape, but a tap
         * does nothing.
         */
        bool dim = !storage_ready_ || (recording_ && !active)
                   || (!recording_ && profile && signals == 0);

        if (!storage_ready_)
        {
            name = "N/A";
            snprintf(hint, sizeof(hint), "no log storage");
        }
        else if (active)
        {
            name = portrait_ ? "REC" : "RECORDING";
            snprintf(hint, sizeof(hint), "tap to stop");
        }
        else if (dim && !recording_)
        {
            snprintf(hint, sizeof(hint), "not set up");
        }

        lv_obj_set_style_bg_color(b.btn, active ? pal.accent : pal.bg, 0);
        lv_obj_set_style_text_color(b.lbl, dim ? pal.panel_border : pal.fg, 0);
        lv_obj_set_style_text_color(
            b.hint, active ? pal.fg : pal.panel_border, 0);
        lv_label_set_text(b.lbl, name);
        lv_label_set_text(b.hint, hint);
    }

    void LoggingScreen::refresh_erase()
    {
        if (theme_ == nullptr || erase_btn_ == nullptr)
        {
            return;
        }

        /**
         * Button dimmed if there is nothing to erase.
         */
        lv_color_t tone = storage_ready_ ? theme_->palette.warn_hi
                                         : theme_->palette.panel_border;
        lv_obj_set_style_border_color(erase_btn_, tone, 0);
        lv_obj_set_style_text_color(
            erase_lbl_,
            storage_ready_ ? theme_->palette.fg : theme_->palette.panel_border,
            0);
    }

    void LoggingScreen::erase_clicked_cb(lv_event_t *e)
    {
        LoggingScreen *self
            = static_cast<LoggingScreen *>(lv_event_get_user_data(e));

        if (!self->storage_ready_ || self->theme_ == nullptr)
        {
            return; /* nothing to erase */
        }

        /**
         * Unlike the toggle, this is not undoable by pressing again, so
         * it asks first.
         */
        ConfirmPopup popup = create_confirm_popup(*self->theme_,
                                                  "Erase all logs?",
                                                  "Stops recording. No undo.",
                                                  "Erase",
                                                  /* destructive */ true);

        lv_obj_add_event_cb(
            popup.confirm_btn, erase_confirm_cb, LV_EVENT_CLICKED, self);
        lv_obj_add_event_cb(
            popup.cancel_btn, erase_cancel_cb, LV_EVENT_CLICKED, self);

        self->erase_confirm_mbox_ = popup.mbox;
    }

    void LoggingScreen::erase_confirm_cb(lv_event_t *e)
    {
        LoggingScreen *self
            = static_cast<LoggingScreen *>(lv_event_get_user_data(e));

        lv_msgbox_close_async(self->erase_confirm_mbox_);
        self->erase_confirm_mbox_ = nullptr;

        /**
         * No local state is touched here. The caller wipes the store and
         * pushes a fresh view-model straight back through refresh(),
         * which is what zeroes the readouts and drops the toggle to OFF.
         */
        if (self->erase_cb_ != nullptr)
        {
            self->erase_cb_(self->erase_ctx_);
        }
    }

    void LoggingScreen::erase_cancel_cb(lv_event_t *e)
    {
        LoggingScreen *self
            = static_cast<LoggingScreen *>(lv_event_get_user_data(e));

        lv_msgbox_close_async(self->erase_confirm_mbox_);
        self->erase_confirm_mbox_ = nullptr;
    }

    void LoggingScreen::button_clicked_cb(lv_event_t *e)
    {
        SourceButton *b
            = static_cast<SourceButton *>(lv_event_get_user_data(e));
        LoggingScreen *self = b->owner;

        if (!self->storage_ready_)
        {
            return; /* nothing to record onto */
        }

        if (self->recording_)
        {
            if (self->source_ != b->source)
            {
                return;
            }
            self->recording_ = false;
        }
        else
        {
            if (b->source == LogSourceVm::Profile
                && self->profile_signals_ == 0)
            {
                return; /* no profile set up on the phone yet */
            }
            self->recording_ = true;
            self->source_    = b->source;
        }
        self->refresh_buttons();

        if (self->toggle_cb_ != nullptr)
        {
            self->toggle_cb_(self->toggle_ctx_, self->recording_, b->source);
        }
    }

    void LoggingScreen::close_clicked_cb(lv_event_t *e)
    {
        LoggingScreen *self
            = static_cast<LoggingScreen *>(lv_event_get_user_data(e));

        if (self->return_screen_ != nullptr)
        {
            lv_screen_load(self->return_screen_);
        }
    }

} // namespace gui
