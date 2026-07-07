#include "settings_screen.hpp"

#include <stdio.h>

#include "layout.hpp"
#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        /* Nav bar (0-44), then Brightness/Panels as two compressed
         * equal-ish sections (divider -> label(+14) -> control row(+38)),
         * plus a third compact Theme row sized like ConfiguratorScreen's
         * tappable rows rather than a third full section — a color swatch
         * grid doesn't need a whole section's worth of room the way a
         * slider or button matrix does. */
        constexpr lv_coord_t HEADER_DIVIDER_Y   = 44;
        constexpr lv_coord_t BRIGHTNESS_LABEL_Y = 58;
        /* Row holding both the slider (native/default height) and the
         * taller Day/Night button — the button is vertically centered on
         * the slider (see LV_ALIGN_OUT_RIGHT_MID below), not stretched to
         * match its height, since a slider's *own* height sets its track
         * thickness in LVGL rather than just its knob travel — forcing it
         * tall turns the whole track into a fat pill. */
        constexpr lv_coord_t BRIGHTNESS_ROW_Y  = 108;
        constexpr lv_coord_t DIVIDER1_Y        = 156;
        constexpr lv_coord_t PANELS_LABEL_Y    = 164;
        constexpr lv_coord_t PANELS_MATRIX_Y   = 204;
        constexpr lv_coord_t DIVIDER2_Y        = 262;
        constexpr lv_coord_t THEME_ROW_Y       = 270;
        constexpr lv_coord_t THEME_ROW_HEIGHT  = 36;
        constexpr lv_coord_t THEME_SWATCH_SIZE = 24;

        constexpr lv_coord_t SCREEN_MARGIN = 20;
        /* Sized to match a single button in the Panels matrix below: same
         * button count/gap/margin formula, so the two read as the same
         * control size even though they're separate widgets. */
        constexpr uint8_t    PANEL_BTN_COUNT      = 6;
        constexpr lv_coord_t MATRIX_BTN_GAP       = 8;
        constexpr lv_coord_t DAY_NIGHT_BTN_HEIGHT = 50;
        constexpr lv_coord_t SLIDER_BTN_GAP       = 32;

        constexpr int32_t BRIGHTNESS_STEP = 5;

        /* Restyles a checkable lv_buttonmatrix away from LVGL's default
         * light-mode card+shadow look to match the flat, bordered,
         * accent-on-checked treatment used everywhere else in this app —
         * used by the Panels matrix below. */
        void style_flat_matrix(lv_obj_t *matrix, const Theme &theme)
        {
            lv_obj_set_style_bg_opa(matrix, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(matrix, 0, 0);
            lv_obj_set_style_shadow_width(matrix, 0, 0);
            lv_obj_set_style_pad_all(matrix, 0, 0);
            lv_obj_set_style_pad_gap(matrix, 8, 0);

            lv_obj_set_style_bg_color(matrix, theme.palette.bg, LV_PART_ITEMS);
            lv_obj_set_style_bg_opa(matrix, LV_OPA_COVER, LV_PART_ITEMS);
            lv_obj_set_style_text_color(
                matrix, theme.palette.fg, LV_PART_ITEMS);
            lv_obj_set_style_radius(matrix, 8, LV_PART_ITEMS);
            lv_obj_set_style_border_width(
                matrix, theme.metrics.panel_border_width, LV_PART_ITEMS);
            lv_obj_set_style_border_color(
                matrix, theme.palette.panel_border, LV_PART_ITEMS);
            lv_obj_set_style_shadow_width(matrix, 0, LV_PART_ITEMS);
            lv_obj_set_style_bg_color(
                matrix, theme.palette.accent, LV_PART_ITEMS | LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(
                matrix, theme.palette.accent, LV_PART_ITEMS | LV_STATE_PRESSED);
        }
    } // namespace

    void SettingsScreen::init(const Theme &theme,
                              uint8_t      initial_day_brightness_pct,
                              uint8_t      initial_night_brightness_pct,
                              uint8_t      initial_num_panels,
                              uint32_t     initial_accent_color,
                              DoneCallback done_cb,
                              void        *done_ctx,
                              PreviewBrightnessCallback preview_cb,
                              void                     *preview_ctx,
                              PickThemeCallback         pick_theme_cb,
                              void                     *pick_theme_ctx)
    {
        day_brightness_pct_   = initial_day_brightness_pct;
        night_brightness_pct_ = initial_night_brightness_pct;
        num_panels_           = initial_num_panels;
        accent_color_         = initial_accent_color;
        done_cb_              = done_cb;
        done_ctx_             = done_ctx;
        preview_cb_           = preview_cb;
        preview_ctx_          = preview_ctx;
        pick_theme_cb_        = pick_theme_cb;
        pick_theme_ctx_       = pick_theme_ctx;

        screen_ = create_screen(theme);

        /* Nav bar: title + close, standing in for the bottom-right
         * checkmark other screens use (see the file doc comment). */
        lv_obj_t *title_lbl = lv_label_create(screen_);
        lv_label_set_text(title_lbl, "Settings");
        lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 20, 12);
        lv_obj_set_style_text_color(title_lbl, theme.palette.fg, 0);
        lv_obj_set_style_text_font(title_lbl, theme.typography.title, 0);

        DoneButton close = create_close_button(screen_, theme);
        lv_obj_add_event_cb(close.btn, done_cb_handler, LV_EVENT_CLICKED, this);

        create_divider(screen_, theme, HEADER_DIVIDER_Y);

        /* Brightness section */
        lv_obj_t *brightness_label = lv_label_create(screen_);
        lv_label_set_text(brightness_label, "Brightness");
        lv_obj_align(
            brightness_label, LV_ALIGN_TOP_LEFT, 20, BRIGHTNESS_LABEL_Y);
        lv_obj_set_style_text_color(brightness_label, theme.palette.fg, 0);
        lv_obj_set_style_text_font(brightness_label, theme.typography.ui, 0);

        brightness_value_lbl_ = lv_label_create(screen_);
        lv_obj_set_style_text_color(brightness_value_lbl_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(
            brightness_value_lbl_, theme.typography.ui, 0);
        lv_obj_align(brightness_value_lbl_,
                     LV_ALIGN_TOP_RIGHT,
                     -SCREEN_MARGIN,
                     BRIGHTNESS_LABEL_Y);

        /* Day/Night button is sized to match one Panels-matrix button
         * (same count/gap/margin formula) and reserves that width plus a
         * gap out of the slider's row, so the slider ends, a little
         * space, then the toggle sits flush against the right margin. */
        lv_coord_t panels_row_width
            = theme.metrics.screen_width - 2 * SCREEN_MARGIN;
        lv_coord_t day_night_btn_width
            = (panels_row_width - (PANEL_BTN_COUNT - 1) * MATRIX_BTN_GAP)
              / PANEL_BTN_COUNT;
        lv_coord_t slider_width
            = panels_row_width - SLIDER_BTN_GAP - day_night_btn_width;

        brightness_slider_ = lv_slider_create(screen_);
        lv_obj_set_width(brightness_slider_, slider_width);
        lv_obj_align(brightness_slider_,
                     LV_ALIGN_TOP_LEFT,
                     SCREEN_MARGIN,
                     BRIGHTNESS_ROW_Y);
        lv_slider_set_range(brightness_slider_, 0, 100);
        lv_obj_set_style_bg_color(
            brightness_slider_, theme.palette.accent, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(
            brightness_slider_, theme.palette.accent, LV_PART_KNOB);
        lv_obj_add_event_cb(brightness_slider_,
                            brightness_changed_cb,
                            LV_EVENT_VALUE_CHANGED,
                            this);

        /* Day/Night is a fast manual switch, not automatic (no clock/
         * light-sensor wiring yet) — flipping it immediately re-applies
         * the real backlight via preview_cb_ so it's actually useful
         * mid-drive, not just a setting you'd tune once and forget. A
         * single button whose glyph swaps between sun/moon
         * (theme.typography.icon_day_night) on each tap, rather than a
         * two-button matrix — one button that toggles reads more like a
         * standard day/night switch than two icon buttons side by side. */
        day_night_btn_ = lv_btn_create(screen_);
        lv_obj_set_size(
            day_night_btn_, day_night_btn_width, DAY_NIGHT_BTN_HEIGHT);
        lv_obj_align_to(day_night_btn_,
                        brightness_slider_,
                        LV_ALIGN_OUT_RIGHT_MID,
                        SLIDER_BTN_GAP,
                        0);
        lv_obj_set_style_bg_color(day_night_btn_, theme.palette.bg, 0);
        lv_obj_set_style_bg_opa(day_night_btn_, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(day_night_btn_, 8, 0);
        lv_obj_set_style_border_width(
            day_night_btn_, theme.metrics.panel_border_width, 0);
        lv_obj_set_style_border_color(
            day_night_btn_, theme.palette.panel_border, 0);
        lv_obj_set_style_shadow_width(day_night_btn_, 0, 0);
        lv_obj_set_style_bg_color(
            day_night_btn_, theme.palette.accent, LV_STATE_PRESSED);
        lv_obj_add_event_cb(
            day_night_btn_, day_night_clicked_cb, LV_EVENT_CLICKED, this);

        day_night_icon_lbl_ = lv_label_create(day_night_btn_);
        lv_obj_set_style_text_font(
            day_night_icon_lbl_, theme.typography.icon_day_night, 0);
        lv_obj_set_style_text_color(day_night_icon_lbl_, theme.palette.fg, 0);
        lv_obj_center(day_night_icon_lbl_);

        create_divider(screen_, theme, DIVIDER1_Y);

        /* Panels section */
        lv_obj_t *panel_count_label = lv_label_create(screen_);
        lv_obj_set_style_text_font(panel_count_label, theme.typography.ui, 0);
        lv_obj_set_style_text_color(panel_count_label, theme.palette.fg, 0);
        lv_label_set_text(panel_count_label, "Panels");
        lv_obj_align(panel_count_label, LV_ALIGN_TOP_LEFT, 20, PANELS_LABEL_Y);

        /* A single row of buttons instead of a vertical roller — the
         * roller's 3 visible rows stood tall enough to cover the "Panels"
         * label above it; this is a fixed, compact height regardless of
         * how many options there are. */
        static const char *const panel_count_map[]
            = { "1", "2", "3", "4", "5", "6", "" };
        panel_count_matrix_ = lv_buttonmatrix_create(screen_);
        lv_obj_set_style_text_font(panel_count_matrix_, theme.typography.ui, 0);
        lv_obj_set_size(
            panel_count_matrix_, theme.metrics.screen_width - 40, 50);
        lv_obj_align(panel_count_matrix_, LV_ALIGN_TOP_MID, 0, PANELS_MATRIX_Y);
        lv_buttonmatrix_set_map(panel_count_matrix_, panel_count_map);
        lv_buttonmatrix_set_one_checked(panel_count_matrix_, true);
        lv_buttonmatrix_set_button_ctrl_all(panel_count_matrix_,
                                            LV_BUTTONMATRIX_CTRL_CHECKABLE);
        style_flat_matrix(panel_count_matrix_, theme);

        create_divider(screen_, theme, DIVIDER2_Y);

        /* Theme row: tappable, hands off to ThemePickerScreen rather than
         * embedding a color grid here — same reasoning as Configurator's
         * Signal/Warning Thresholds rows. */
        lv_obj_t *theme_row_btn = lv_btn_create(screen_);
        lv_obj_set_size(
            theme_row_btn, theme.metrics.screen_width - 40, THEME_ROW_HEIGHT);
        lv_obj_align(theme_row_btn, LV_ALIGN_TOP_MID, 0, THEME_ROW_Y);
        lv_obj_set_style_bg_opa(theme_row_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(theme_row_btn, 0, 0);
        lv_obj_set_style_shadow_width(theme_row_btn, 0, 0);
        lv_obj_set_style_pad_all(theme_row_btn, 0, 0);
        lv_obj_set_style_bg_color(
            theme_row_btn, theme.palette.accent, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(theme_row_btn, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_add_event_cb(
            theme_row_btn, pick_theme_cb_handler, LV_EVENT_CLICKED, this);

        lv_obj_t *theme_label = lv_label_create(theme_row_btn);
        lv_label_set_text(theme_label, "Theme");
        lv_obj_set_style_text_color(theme_label, theme.palette.fg, 0);
        lv_obj_set_style_text_font(theme_label, theme.typography.ui, 0);
        lv_obj_align(theme_label, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *theme_chevron = lv_label_create(theme_row_btn);
        lv_obj_set_style_text_font(
            theme_chevron, theme.typography.symbol_lg, 0);
        lv_label_set_text(theme_chevron, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(theme_chevron, theme.palette.fg, 0);
        lv_obj_align(theme_chevron, LV_ALIGN_RIGHT_MID, 0, 0);

        /* Current-accent preview — a plain colored square rather than a
         * text swatch name, since the presets don't have user-facing
         * names beyond their appearance. */
        theme_swatch_ = lv_obj_create(theme_row_btn);
        lv_obj_set_size(theme_swatch_, THEME_SWATCH_SIZE, THEME_SWATCH_SIZE);
        lv_obj_set_style_radius(theme_swatch_, 6, 0);
        lv_obj_set_style_border_width(
            theme_swatch_, theme.metrics.panel_border_width, 0);
        lv_obj_set_style_border_color(
            theme_swatch_, theme.palette.panel_border, 0);
        lv_obj_set_style_shadow_width(theme_swatch_, 0, 0);
        lv_obj_align_to(
            theme_swatch_, theme_chevron, LV_ALIGN_OUT_LEFT_MID, -12, 0);
    }

    void SettingsScreen::open()
    {
        /* Reopens showing whichever of day/night was last selected, not
         * always Day — flipping to Night to dim the screen and then
         * backing out of Settings shouldn't silently flip back. */
        lv_slider_set_value(brightness_slider_,
                            showing_night_ ? night_brightness_pct_
                                           : day_brightness_pct_,
                            LV_ANIM_OFF);
        refresh_brightness_label();
        refresh_day_night_icon();

        uint8_t selected = (num_panels_ >= 1 && num_panels_ <= 6)
                               ? static_cast<uint8_t>(num_panels_ - 1)
                               : 4;
        lv_buttonmatrix_set_button_ctrl(
            panel_count_matrix_, selected, LV_BUTTONMATRIX_CTRL_CHECKED);

        refresh_theme_swatch();

        lv_screen_load_anim(
            screen_, LV_SCR_LOAD_ANIM_OVER_BOTTOM, 220, 0, false);
    }

    void SettingsScreen::set_accent_color(uint32_t accent_color)
    {
        accent_color_ = accent_color;
        refresh_theme_swatch();
    }

    void SettingsScreen::refresh_brightness_label()
    {
        char buf[8];
        snprintf(buf,
                 sizeof(buf),
                 "%d%%",
                 static_cast<int>(lv_slider_get_value(brightness_slider_)));
        lv_label_set_text(brightness_value_lbl_, buf);
    }

    void SettingsScreen::refresh_theme_swatch()
    {
        lv_obj_set_style_bg_color(
            theme_swatch_, lv_color_hex(accent_color_), 0);
        lv_obj_set_style_bg_opa(theme_swatch_, LV_OPA_COVER, 0);
    }

    void SettingsScreen::refresh_day_night_icon()
    {
        lv_label_set_text(day_night_icon_lbl_,
                          showing_night_ ? ICON_MOON : ICON_SUN);
    }

    void SettingsScreen::capture_slider_value()
    {
        uint8_t value
            = static_cast<uint8_t>(lv_slider_get_value(brightness_slider_));
        if (showing_night_)
        {
            night_brightness_pct_ = value;
        }
        else
        {
            day_brightness_pct_ = value;
        }
    }

    void SettingsScreen::brightness_changed_cb(lv_event_t *e)
    {
        auto *self = static_cast<SettingsScreen *>(lv_event_get_user_data(e));

        /* lv_slider has no native step option — snap to the nearest 5%
         * here so dragging feels stepped instead of continuous. */
        int32_t raw = lv_slider_get_value(self->brightness_slider_);
        int32_t snapped
            = ((raw + BRIGHTNESS_STEP / 2) / BRIGHTNESS_STEP) * BRIGHTNESS_STEP;
        if (snapped != raw)
        {
            lv_slider_set_value(self->brightness_slider_, snapped, LV_ANIM_OFF);
        }

        self->refresh_brightness_label();
        self->capture_slider_value();
        if (self->preview_cb_ != nullptr)
        {
            self->preview_cb_(self->preview_ctx_,
                              static_cast<uint8_t>(lv_slider_get_value(
                                  self->brightness_slider_)));
        }
    }

    void SettingsScreen::day_night_clicked_cb(lv_event_t *e)
    {
        auto *self = static_cast<SettingsScreen *>(lv_event_get_user_data(e));

        self->capture_slider_value();
        self->showing_night_ = !self->showing_night_;
        self->refresh_day_night_icon();

        uint8_t value = self->showing_night_ ? self->night_brightness_pct_
                                             : self->day_brightness_pct_;
        lv_slider_set_value(self->brightness_slider_, value, LV_ANIM_ON);
        self->refresh_brightness_label();

        if (self->preview_cb_ != nullptr)
        {
            self->preview_cb_(self->preview_ctx_, value);
        }
    }

    void SettingsScreen::pick_theme_cb_handler(lv_event_t *e)
    {
        auto *self = static_cast<SettingsScreen *>(lv_event_get_user_data(e));
        if (self->pick_theme_cb_ != nullptr)
        {
            self->pick_theme_cb_(self->pick_theme_ctx_);
        }
    }

    void SettingsScreen::done_cb_handler(lv_event_t *e)
    {
        auto *self = static_cast<SettingsScreen *>(lv_event_get_user_data(e));

        self->capture_slider_value();

        /* get_selected_button() tracks focus/last-press, not the checked
         * state itself, so it can't be trusted if Done is pressed without
         * ever touching the matrix — scan for whichever button is actually
         * checked instead. */
        for (uint8_t i = 0; i < 6; i++)
        {
            if (lv_buttonmatrix_has_button_ctrl(
                    self->panel_count_matrix_, i, LV_BUTTONMATRIX_CTRL_CHECKED))
            {
                self->num_panels_ = static_cast<uint8_t>(i + 1);
                break;
            }
        }

        if (self->done_cb_ != nullptr)
        {
            self->done_cb_(self->done_ctx_,
                           self->day_brightness_pct_,
                           self->night_brightness_pct_,
                           self->num_panels_);
        }
    }

} // namespace gui
