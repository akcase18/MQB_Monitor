#include "settings_screen.hpp"

#include <stdio.h>

#include "layout.hpp"
#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        /* Nav bar, then Brightness and Panels as single label+control rows
         * (not label-above-control — that cost a whole extra line each,
         * and this screen doesn't have room to spare), then a footer of
         * two compact tappable rows (Theme, Wireless Setup). Every row is
         * separated from its neighbor by a divider so each reads as its
         * own distinct row instead of blurring into the next one. */
        constexpr lv_coord_t HEADER_DIVIDER_Y = 50;

        constexpr lv_coord_t BRIGHTNESS_ROW_Y          = 70;
        constexpr lv_coord_t BRIGHTNESS_ROW_HEIGHT     = 40;
        constexpr lv_coord_t BRIGHTNESS_LABEL_GAP      = 24;
        constexpr lv_coord_t BRIGHTNESS_SLIDER_BTN_GAP = 24;
        constexpr lv_coord_t DIVIDER1_Y                = 120;

        constexpr lv_coord_t PANELS_ROW_Y      = 136;
        constexpr lv_coord_t PANELS_ROW_HEIGHT = 40;
        constexpr lv_coord_t PANELS_LABEL_GAP  = 10;
        /* Matches style_flat_matrix()'s pad_gap below — used to compute a
         * single panel-count button's width so the Day/Night button
         * (built first, in the Brightness row above this one) can match
         * it exactly rather than being an arbitrary square. */
        constexpr uint8_t    PANEL_BTN_COUNT = 6;
        constexpr lv_coord_t PANEL_BTN_GAP   = 8;
        constexpr lv_coord_t DIVIDER2_Y      = 186;

        constexpr lv_coord_t THEME_ROW_Y        = 202;
        constexpr lv_coord_t DIVIDER3_Y         = 248;
        constexpr lv_coord_t WIRELESS_ROW_Y     = 262;
        constexpr lv_coord_t COMPACT_ROW_HEIGHT = 38;
        constexpr lv_coord_t THEME_SWATCH_SIZE  = 22;

        constexpr lv_coord_t SCREEN_MARGIN = 20;

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
                              void                     *pick_theme_ctx,
                              PickWirelessCallback      pick_wireless_cb,
                              void                     *pick_wireless_ctx)
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
        pick_wireless_cb_     = pick_wireless_cb;
        pick_wireless_ctx_    = pick_wireless_ctx;

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

        /* Brightness section — label, slider, and the Day/Night toggle
         * all share a single row (no separate "100%" readout — the
         * slider's own knob position already shows the value, and a
         * numeric readout wasn't earning its keep). */
        lv_obj_t *brightness_row = lv_obj_create(screen_);
        lv_obj_remove_style_all(brightness_row);
        lv_obj_set_size(brightness_row,
                        theme.metrics.screen_width - 2 * SCREEN_MARGIN,
                        BRIGHTNESS_ROW_HEIGHT);
        lv_obj_align(
            brightness_row, LV_ALIGN_TOP_LEFT, SCREEN_MARGIN, BRIGHTNESS_ROW_Y);

        lv_obj_t *brightness_label = lv_label_create(brightness_row);
        lv_label_set_text(brightness_label, "Brightness");
        lv_obj_set_style_text_color(brightness_label, theme.palette.fg, 0);
        lv_obj_set_style_text_font(
            brightness_label, theme.typography.label_lg, 0);
        lv_obj_align(brightness_label, LV_ALIGN_LEFT_MID, 0, 0);

        /* Measured, not guessed — a hardcoded reserved width drifted out
         * of sync with this label's actual rendered width (a monospace
         * UI font renders wider per character than it looks like it
         * should) and let the slider start underneath the text. */
        lv_obj_update_layout(brightness_label);
        lv_coord_t brightness_label_width = lv_obj_get_width(brightness_label);

        /* A throwaway probe, deleted immediately after measuring — lets
         * the Day/Night button (built next) be sized to exactly match a
         * real Panels-matrix button before that matrix exists yet, without
         * duplicating PANEL_BTN_COUNT/PANEL_BTN_GAP's math against a
         * guessed label width. */
        lv_obj_t *panels_label_probe = lv_label_create(screen_);
        lv_obj_set_style_text_font(
            panels_label_probe, theme.typography.label_lg, 0);
        lv_label_set_text(panels_label_probe, "Panels");
        lv_obj_update_layout(panels_label_probe);
        lv_coord_t panels_matrix_width
            = (theme.metrics.screen_width - 2 * SCREEN_MARGIN)
              - lv_obj_get_width(panels_label_probe) - PANELS_LABEL_GAP;
        lv_obj_delete(panels_label_probe);
        lv_coord_t panel_btn_width
            = (panels_matrix_width - (PANEL_BTN_COUNT - 1) * PANEL_BTN_GAP)
              / PANEL_BTN_COUNT;

        /* Day/Night is a fast manual switch, not automatic (no clock/
         * light-sensor wiring yet) — flipping it immediately re-applies
         * the real backlight via preview_cb_ so it's actually useful
         * mid-drive, not just a setting you'd tune once and forget. A
         * single button whose glyph swaps between sun/moon
         * (theme.typography.icon_day_night) on each tap, rather than a
         * two-button matrix — one button that toggles reads more like a
         * standard day/night switch than two icon buttons side by side.
         * Sized to match a single Panels-matrix button exactly, so the
         * two rows' rightmost controls read as the same control size. */
        day_night_btn_ = lv_btn_create(brightness_row);
        lv_obj_set_size(day_night_btn_, panel_btn_width, BRIGHTNESS_ROW_HEIGHT);
        lv_obj_align(day_night_btn_, LV_ALIGN_RIGHT_MID, 0, 0);
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

        /* Slider fills whatever's left between the label's actual width
         * and the Day/Night button. */
        lv_coord_t slider_width
            = (theme.metrics.screen_width - 2 * SCREEN_MARGIN)
              - brightness_label_width - BRIGHTNESS_LABEL_GAP - panel_btn_width
              - BRIGHTNESS_SLIDER_BTN_GAP;

        brightness_slider_ = lv_slider_create(brightness_row);
        lv_obj_set_width(brightness_slider_, slider_width);
        lv_obj_align(brightness_slider_,
                     LV_ALIGN_LEFT_MID,
                     brightness_label_width + BRIGHTNESS_LABEL_GAP,
                     0);
        lv_slider_set_range(brightness_slider_, 0, 100);
        lv_obj_set_style_bg_color(
            brightness_slider_, theme.palette.accent, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(
            brightness_slider_, theme.palette.accent, LV_PART_KNOB);
        lv_obj_add_event_cb(brightness_slider_,
                            brightness_changed_cb,
                            LV_EVENT_VALUE_CHANGED,
                            this);

        create_divider(screen_, theme, DIVIDER1_Y);

        /* Panels section — label and the 1-6 count matrix share a single
         * row (label left, matrix filling the rest of the width) rather
         * than label-above-matrix, which cost a whole extra line of
         * vertical space this screen doesn't have to spare. */
        lv_obj_t *panels_row = lv_obj_create(screen_);
        lv_obj_remove_style_all(panels_row);
        lv_obj_set_size(panels_row,
                        theme.metrics.screen_width - 2 * SCREEN_MARGIN,
                        PANELS_ROW_HEIGHT);
        lv_obj_align(
            panels_row, LV_ALIGN_TOP_LEFT, SCREEN_MARGIN, PANELS_ROW_Y);

        lv_obj_t *panel_count_label = lv_label_create(panels_row);
        lv_obj_set_style_text_font(
            panel_count_label, theme.typography.label_lg, 0);
        lv_obj_set_style_text_color(panel_count_label, theme.palette.fg, 0);
        lv_label_set_text(panel_count_label, "Panels");
        lv_obj_align(panel_count_label, LV_ALIGN_LEFT_MID, 0, 0);

        /* Measured, not guessed — see the matching comment on Brightness's
         * label above. */
        lv_obj_update_layout(panel_count_label);
        lv_coord_t panels_label_width = lv_obj_get_width(panel_count_label);

        /* A row of buttons instead of a vertical roller — the roller's 3
         * visible rows stood tall enough to cover the "Panels" label
         * above it; this is a fixed, compact height regardless of how
         * many options there are. */
        static const char *const panel_count_map[]
            = { "1", "2", "3", "4", "5", "6", "" };
        panel_count_matrix_ = lv_buttonmatrix_create(panels_row);
        lv_obj_set_style_text_font(panel_count_matrix_, theme.typography.ui, 0);
        lv_obj_set_size(panel_count_matrix_,
                        theme.metrics.screen_width - 2 * SCREEN_MARGIN
                            - panels_label_width - PANELS_LABEL_GAP,
                        PANELS_ROW_HEIGHT);
        lv_obj_align(panel_count_matrix_, LV_ALIGN_RIGHT_MID, 0, 0);
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
            theme_row_btn, theme.metrics.screen_width - 40, COMPACT_ROW_HEIGHT);
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
        lv_obj_set_style_text_font(theme_label, theme.typography.label_lg, 0);
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

        create_divider(screen_, theme, DIVIDER3_Y);

        /* Wireless Setup row: tappable, hands off to WirelessSetupScreen —
         * same shape as the Theme row above it, just without a preview
         * swatch (there's no "current value" to show at a glance). */
        lv_obj_t *wireless_row_btn = lv_btn_create(screen_);
        lv_obj_set_size(wireless_row_btn,
                        theme.metrics.screen_width - 40,
                        COMPACT_ROW_HEIGHT);
        lv_obj_align(wireless_row_btn, LV_ALIGN_TOP_MID, 0, WIRELESS_ROW_Y);
        lv_obj_set_style_bg_opa(wireless_row_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(wireless_row_btn, 0, 0);
        lv_obj_set_style_shadow_width(wireless_row_btn, 0, 0);
        lv_obj_set_style_pad_all(wireless_row_btn, 0, 0);
        lv_obj_set_style_bg_color(
            wireless_row_btn, theme.palette.accent, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(wireless_row_btn, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_add_event_cb(
            wireless_row_btn, pick_wireless_cb_handler, LV_EVENT_CLICKED, this);

        lv_obj_t *wireless_label = lv_label_create(wireless_row_btn);
        lv_label_set_text(wireless_label, "Wireless Setup");
        lv_obj_set_style_text_color(wireless_label, theme.palette.fg, 0);
        lv_obj_set_style_text_font(
            wireless_label, theme.typography.label_lg, 0);
        lv_obj_align(wireless_label, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *wireless_chevron = lv_label_create(wireless_row_btn);
        lv_obj_set_style_text_font(
            wireless_chevron, theme.typography.symbol_lg, 0);
        lv_label_set_text(wireless_chevron, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(wireless_chevron, theme.palette.fg, 0);
        lv_obj_align(wireless_chevron, LV_ALIGN_RIGHT_MID, 0, 0);
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

    void SettingsScreen::pick_wireless_cb_handler(lv_event_t *e)
    {
        auto *self = static_cast<SettingsScreen *>(lv_event_get_user_data(e));
        if (self->pick_wireless_cb_ != nullptr)
        {
            self->pick_wireless_cb_(self->pick_wireless_ctx_);
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
