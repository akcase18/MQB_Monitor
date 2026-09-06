#include "logging_screen.hpp"
#include "mem_placement.hpp"

#include <stdio.h>

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        /* Nav bar, one oversized toggle, the status tiles, then the
         * erase action. The toggle gets far more room than any control on
         * SettingsScreen: it's the whole point of the screen and it gets
         * pressed while driving, so it's sized to be hit without looking
         * rather than to fit alongside neighbors.
         *
         * Landscape's 320px of height can't hold the toggle, four
         * full-width status rows and a button, which is why the tiles sit
         * two-up here (see the header comment) -- two rows of tiles
         * instead of four rows is what pays for the erase button.
         * Erase is last on purpose: furthest from the toggle the user
         * actually reaches for, and below the readouts that say whether
         * there's anything worth keeping. */
        constexpr lv_coord_t HEADER_DIVIDER_Y = 50;

        constexpr lv_coord_t TOGGLE_ROW_Y     = 60;
        constexpr lv_coord_t TOGGLE_HEIGHT    = 58;
        constexpr lv_coord_t TOGGLE_DIVIDER_Y = 130;

        constexpr lv_coord_t STAT_FIRST_Y   = 140;
        constexpr lv_coord_t STAT_HEIGHT    = 50;
        constexpr lv_coord_t STAT_STEP      = 54;
        /* Landscape only -- Portrait stacks all four in one column. */
        constexpr uint8_t    STAT_COLUMNS   = 2;
        constexpr lv_coord_t STAT_COLUMN_GAP = 16;

        constexpr lv_coord_t ERASE_DIVIDER_Y = 252;
        constexpr lv_coord_t ERASE_ROW_Y     = 260;
        constexpr lv_coord_t ERASE_HEIGHT    = 44;

        /* Portrait has ~160px more height to spend, so everything gets
         * taller and further apart rather than sitting in a cluster at
         * the top -- the same treatment SettingsScreen's Portrait pass
         * applies for the same reason. The four tiles stay in one column
         * (a 2x2 grid at 320px wide would leave the usage figure a cell
         * too narrow), and there's still room for the erase button under
         * them. */
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

        constexpr uint8_t STATUS_ROW_COUNT = 4;

        /** A status tile: fixed caption above, value below, both left-
         * aligned. Returns the value label, which is the only half that
         * ever changes.
         *
         * Stacked rather than caption-left/value-right so the value gets
         * the tile's full width -- see the header comment. Captions sit
         * back in panel_border, the theme's "structural, not content"
         * tone, which keeps the eye on the numbers. */
        FLASHMEM lv_obj_t *create_status_tile(lv_obj_t    *parent,
                                              const Theme &theme,
                                              const char  *caption,
                                              lv_coord_t   x,
                                              lv_coord_t   y,
                                              lv_coord_t   width,
                                              lv_coord_t   height)
        {
            lv_obj_t *tile = lv_obj_create(parent);
            lv_obj_remove_style_all(tile);
            lv_obj_set_size(tile, width, height);
            lv_obj_align(tile, LV_ALIGN_TOP_LEFT, x, y);

            lv_obj_t *caption_lbl = lv_label_create(tile);
            lv_label_set_text(caption_lbl, caption);
            lv_obj_set_style_text_font(
                caption_lbl, theme.typography.label_lg, 0);
            lv_obj_set_style_text_color(
                caption_lbl, theme.palette.panel_border, 0);
            lv_obj_align(caption_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

            lv_obj_t *value_lbl = lv_label_create(tile);
            lv_label_set_text(value_lbl, "--");
            lv_obj_set_style_text_font(value_lbl, theme.typography.ui, 0);
            lv_obj_set_style_text_color(value_lbl, theme.palette.fg, 0);
            lv_obj_align(value_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

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
                                      void          *toggle_ctx,
                                      EraseCallback  erase_cb,
                                      void          *erase_ctx)
    {
        theme_       = &theme;
        toggle_cb_   = toggle_cb;
        toggle_ctx_  = toggle_ctx;
        erase_cb_    = erase_cb;
        erase_ctx_   = erase_ctx;

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
            = portrait ? PORTRAIT_STAT_FIRST_Y : STAT_FIRST_Y;
        lv_coord_t step   = portrait ? PORTRAIT_STAT_STEP : STAT_STEP;
        lv_coord_t height = portrait ? PORTRAIT_STAT_HEIGHT : STAT_HEIGHT;

        /* One column in Portrait, two in Landscape -- so the same four
         * tiles walk down a single column or fill a 2x2 grid without two
         * separate placement loops. */
        uint8_t    columns = portrait ? 1 : STAT_COLUMNS;
        lv_coord_t tile_width
            = portrait
                  ? row_width
                  : static_cast<lv_coord_t>((row_width - STAT_COLUMN_GAP)
                                            / STAT_COLUMNS);
        lv_coord_t column_step
            = static_cast<lv_coord_t>(tile_width + STAT_COLUMN_GAP);

        const char *captions[STATUS_ROW_COUNT]
            = { "Logs Saved", "Storage", "Used", "Recorded" };
        lv_obj_t **values[STATUS_ROW_COUNT] = { &count_value_,
                                                &storage_value_,
                                                &capacity_value_,
                                                &recorded_value_ };

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

        /* Same full-width shape as the toggle, deliberately not the same
         * weight: bordered in warn_hi rather than filled, so it reads as
         * the one destructive control on the screen without competing
         * with the control the user actually came here for. */
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

        refresh_toggle();
        refresh_erase();
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
        refresh_erase();

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

    void LoggingScreen::refresh_erase()
    {
        if (theme_ == nullptr || erase_btn_ == nullptr)
        {
            return;
        }

        /* Nothing mounted means nothing to wipe. Dimmed rather than
         * hidden, for the same reason the toggle is: the screen should
         * still explain its own shape when storage is missing, not lose
         * half its controls. */
        lv_color_t tone = storage_ready_ ? theme_->palette.warn_hi
                                         : theme_->palette.panel_border;
        lv_obj_set_style_border_color(erase_btn_, tone, 0);
        lv_obj_set_style_text_color(erase_lbl_,
                                    storage_ready_
                                        ? theme_->palette.fg
                                        : theme_->palette.panel_border,
                                    0);
    }

    void LoggingScreen::erase_clicked_cb(lv_event_t *e)
    {
        LoggingScreen *self
            = static_cast<LoggingScreen *>(lv_event_get_user_data(e));

        if (!self->storage_ready_ || self->theme_ == nullptr)
        {
            return; /* nothing to erase -- see refresh_erase() */
        }

        /* Unlike the toggle, this is not undoable by pressing again, so
         * it asks first -- the same Cancel/Confirm popup rotating the
         * display sits behind. The body names the one surprising side
         * effect: the caller stops recording as part of erasing, rather
         * than wiping the store out from under a capture still writing
         * into it. */
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

        /* No local state is touched here. The caller wipes the store and
         * pushes a fresh view-model straight back through refresh(),
         * which is what zeroes the readouts and drops the toggle to OFF
         * -- guessing at the result locally could only disagree with what
         * actually happened. */
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
