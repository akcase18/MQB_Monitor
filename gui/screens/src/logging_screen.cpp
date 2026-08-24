#include "logging_screen.hpp"
#include "mem_placement.hpp"

#include <stdio.h>

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        /* Nav bar, then one oversized toggle, then four status rows under
         * a divider. The toggle gets far more room than any control on
         * SettingsScreen: it's the whole point of the screen and it gets
         * pressed while driving, so it's sized to be hit without looking
         * rather than to fit alongside neighbors. */
        constexpr lv_coord_t HEADER_DIVIDER_Y = 50;

        constexpr lv_coord_t TOGGLE_ROW_Y      = 66;
        constexpr lv_coord_t TOGGLE_HEIGHT     = 62;
        constexpr lv_coord_t TOGGLE_DIVIDER_Y  = 142;
        constexpr lv_coord_t STATUS_FIRST_Y    = 156;
        constexpr lv_coord_t STATUS_ROW_HEIGHT = 34;
        constexpr lv_coord_t STATUS_ROW_STEP   = 38;

        /* Portrait has ~160px more height to spend, so everything gets
         * taller and further apart rather than sitting in a cluster at
         * the top — the same treatment SettingsScreen's Portrait pass
         * applies for the same reason. */
        constexpr lv_coord_t PORTRAIT_TOGGLE_ROW_Y      = 72;
        constexpr lv_coord_t PORTRAIT_TOGGLE_HEIGHT     = 84;
        constexpr lv_coord_t PORTRAIT_TOGGLE_DIVIDER_Y  = 176;
        constexpr lv_coord_t PORTRAIT_STATUS_FIRST_Y    = 196;
        constexpr lv_coord_t PORTRAIT_STATUS_ROW_HEIGHT = 44;
        constexpr lv_coord_t PORTRAIT_STATUS_ROW_STEP   = 56;

        constexpr lv_coord_t SCREEN_MARGIN = 20;

        constexpr uint8_t STATUS_ROW_COUNT = 4;

        /** A status line: fixed caption on the left, value on the right.
         * Returns the value label, which is the only half that ever
         * changes. */
        FLASHMEM lv_obj_t *create_status_row(lv_obj_t    *parent,
                                             const Theme &theme,
                                             const char  *caption,
                                             lv_coord_t   y,
                                             lv_coord_t   width,
                                             lv_coord_t   height)
        {
            lv_obj_t *row = lv_obj_create(parent);
            lv_obj_remove_style_all(row);
            lv_obj_set_size(row, width, height);
            lv_obj_align(row, LV_ALIGN_TOP_LEFT, SCREEN_MARGIN, y);

            lv_obj_t *caption_lbl = lv_label_create(row);
            lv_label_set_text(caption_lbl, caption);
            lv_obj_set_style_text_font(
                caption_lbl, theme.typography.label_lg, 0);
            /* Captions sit back from their values -- panel_border is the
             * theme's existing "structural, not content" tone, and it
             * keeps the eye on the numbers. */
            lv_obj_set_style_text_color(
                caption_lbl, theme.palette.panel_border, 0);
            lv_obj_align(caption_lbl, LV_ALIGN_LEFT_MID, 0, 0);

            lv_obj_t *value_lbl = lv_label_create(row);
            lv_label_set_text(value_lbl, "--");
            lv_obj_set_style_text_font(value_lbl, theme.typography.ui, 0);
            lv_obj_set_style_text_color(value_lbl, theme.palette.fg, 0);
            lv_obj_align(value_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

            return value_lbl;
        }

        /**
         * Formats `used`/`total` as "4.2 / 13.0 MB" into `out`.
         *
         * Both numbers share one unit, chosen from `total` and printed
         * once at the end. Giving each its own unit reads worse when
         * they differ ("999.9 KB / 13.0 MB") and, at the value font,
         * overran the caption in Portrait's 280px row -- the same
         * overflow that killed the earlier two-number capacity format.
         * The unit steps MB -> GB because the ring spans a 13MB flash
         * budget to a potentially multi-gigabyte card (see TODO.md).
         *
         * A `used` under ~0.05 of the unit prints as "0.0", which is
         * honest: at that point essentially nothing has been recorded.
         */
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
                                      void          *toggle_ctx)
    {
        theme_       = &theme;
        toggle_cb_   = toggle_cb;
        toggle_ctx_  = toggle_ctx;

        screen_ = create_screen(theme);

        bool portrait
            = theme.metrics.screen_height > theme.metrics.screen_width;

        create_nav_title(screen_, theme, "Logging");

        DoneButton close = create_close_button(screen_, theme);
        lv_obj_add_event_cb(
            close.btn, close_clicked_cb, LV_EVENT_CLICKED, this);

        create_divider(screen_, theme, HEADER_DIVIDER_Y);

        lv_coord_t row_width = theme.metrics.screen_width - 2 * SCREEN_MARGIN;

        /* The toggle is the whole row, not a small control on the right of
         * a labelled row the way SettingsScreen's toggles are -- there's
         * nothing else competing for this space, and a full-width target
         * is far easier to hit in a moving car. */
        toggle_btn_ = lv_btn_create(screen_);
        lv_obj_set_size(toggle_btn_,
                        row_width,
                        portrait ? PORTRAIT_TOGGLE_HEIGHT : TOGGLE_HEIGHT);
        lv_obj_align(toggle_btn_,
                     LV_ALIGN_TOP_LEFT,
                     SCREEN_MARGIN,
                     portrait ? PORTRAIT_TOGGLE_ROW_Y : TOGGLE_ROW_Y);
        lv_obj_set_style_radius(toggle_btn_, 10, 0);
        lv_obj_set_style_bg_opa(toggle_btn_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(
            toggle_btn_, theme.metrics.panel_border_width, 0);
        lv_obj_set_style_border_color(
            toggle_btn_, theme.palette.panel_border, 0);
        lv_obj_set_style_shadow_width(toggle_btn_, 0, 0);
        lv_obj_add_event_cb(
            toggle_btn_, toggle_clicked_cb, LV_EVENT_CLICKED, this);

        toggle_lbl_ = lv_label_create(toggle_btn_);
        lv_obj_set_style_text_font(toggle_lbl_, theme.typography.title, 0);
        lv_obj_align(toggle_lbl_, LV_ALIGN_CENTER, 0, -10);

        /* Second line inside the button: what a press will do next, so
         * the control states its own affordance instead of relying on
         * the user remembering what the fill color means. */
        toggle_hint_ = lv_label_create(toggle_btn_);
        lv_obj_set_style_text_font(toggle_hint_, theme.typography.label_sm, 0);
        lv_obj_align(toggle_hint_, LV_ALIGN_CENTER, 0, 14);

        create_divider(screen_,
                       theme,
                       portrait ? PORTRAIT_TOGGLE_DIVIDER_Y
                                : TOGGLE_DIVIDER_Y);

        lv_coord_t first_y
            = portrait ? PORTRAIT_STATUS_FIRST_Y : STATUS_FIRST_Y;
        lv_coord_t step
            = portrait ? PORTRAIT_STATUS_ROW_STEP : STATUS_ROW_STEP;
        lv_coord_t height
            = portrait ? PORTRAIT_STATUS_ROW_HEIGHT : STATUS_ROW_HEIGHT;

        const char *captions[STATUS_ROW_COUNT]
            = { "Logs Saved", "Storage", "Used", "Recorded" };
        lv_obj_t **values[STATUS_ROW_COUNT] = { &count_value_,
                                                &storage_value_,
                                                &capacity_value_,
                                                &recorded_value_ };

        for (uint8_t i = 0; i < STATUS_ROW_COUNT; i++)
        {
            *values[i] = create_status_row(
                screen_,
                theme,
                captions[i],
                static_cast<lv_coord_t>(first_y + i * step),
                row_width,
                height);
        }

        refresh_toggle();
    }

    void LoggingScreen::open(const LoggingViewModel &vm,
                             lv_obj_t               *return_screen)
    {
        return_screen_ = return_screen;
        set_view_model(vm);
        lv_screen_load_anim(
            screen_, LV_SCR_LOAD_ANIM_OVER_TOP, 220, 0, false);
    }

    void LoggingScreen::refresh(const LoggingViewModel &vm)
    {
        set_view_model(vm);
    }

    void LoggingScreen::set_view_model(const LoggingViewModel &vm)
    {
        recording_     = vm.recording;
        storage_ready_ = vm.storage_ready;
        refresh_toggle();

        char buf[32];

        snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(vm.log_count));
        lv_label_set_text(count_value_, buf);

        lv_label_set_text(storage_value_,
                          vm.storage_label[0] != '\0' ? vm.storage_label
                                                      : "--");

        /* Used against the ring's total. Not "remaining": the ring
         * overwrites rather than filling up, so past the first lap this
         * pins at full and recording simply continues, dropping the
         * oldest chunk each time around. A countdown-to-zero would sit
         * at zero for the life of the device. */
        format_usage(vm.used_bytes, vm.ring_capacity_bytes, buf, sizeof(buf));
        lv_label_set_text(capacity_value_, buf);

        snprintf(buf,
                 sizeof(buf),
                 "%u min",
                 static_cast<unsigned>(vm.total_logged_min));
        lv_label_set_text(recorded_value_, buf);
    }

    void LoggingScreen::refresh_toggle()
    {
        if (theme_ == nullptr)
        {
            return;
        }

        if (!storage_ready_)
        {
            /* No store mounted -- nothing to record onto. Shown as a
             * disabled-looking button rather than hidden, so the screen
             * still explains itself instead of just missing its main
             * control. */
            lv_obj_set_style_bg_color(toggle_btn_, theme_->palette.bg, 0);
            lv_obj_set_style_text_color(
                toggle_lbl_, theme_->palette.panel_border, 0);
            lv_obj_set_style_text_color(
                toggle_hint_, theme_->palette.panel_border, 0);
            lv_label_set_text(toggle_lbl_, "UNAVAILABLE");
            lv_label_set_text(toggle_hint_, "no log storage");
            return;
        }

        /* Recording reads as a solid accent fill, the same treatment
         * every other checked/active control in this app uses (see
         * screen_helpers' style_flat_matrix()). */
        lv_obj_set_style_bg_color(toggle_btn_,
                                  recording_ ? theme_->palette.accent
                                             : theme_->palette.bg,
                                  0);
        lv_obj_set_style_text_color(toggle_lbl_, theme_->palette.fg, 0);
        lv_obj_set_style_text_color(
            toggle_hint_, theme_->palette.panel_border, 0);
        lv_label_set_text(toggle_lbl_, recording_ ? "RECORDING" : "OFF");
        lv_label_set_text(toggle_hint_,
                          recording_ ? "tap to stop" : "tap to start");
    }

    void LoggingScreen::toggle_clicked_cb(lv_event_t *e)
    {
        LoggingScreen *self
            = static_cast<LoggingScreen *>(lv_event_get_user_data(e));

        if (!self->storage_ready_)
        {
            return; /* nothing to record onto -- see refresh_toggle() */
        }

        self->recording_ = !self->recording_;
        self->refresh_toggle();

        if (self->toggle_cb_ != nullptr)
        {
            self->toggle_cb_(self->toggle_ctx_, self->recording_);
        }
    }

    void LoggingScreen::close_clicked_cb(lv_event_t *e)
    {
        LoggingScreen *self
            = static_cast<LoggingScreen *>(lv_event_get_user_data(e));

        if (self->return_screen_ != nullptr)
        {
            /* Mirrors open()'s OVER_TOP with its opposite, so closing
             * visually undoes the swipe that opened this. */
            lv_screen_load_anim(self->return_screen_,
                                LV_SCR_LOAD_ANIM_OVER_BOTTOM,
                                220,
                                0,
                                false);
        }
    }

} // namespace gui
