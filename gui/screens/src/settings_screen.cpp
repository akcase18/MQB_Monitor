#include "settings_screen.hpp"
#include "mem_placement.hpp"

#include <stdio.h>

#include "layout.hpp"
#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        /**
         * Nav bar, then Brightness and Panels as single label+control rows
         * (not label-above-control — that cost a whole extra line each,
         * and this screen doesn't have room to spare), then a footer of
         * two compact tappable rows (Theme|Orientation, WiFi). Every row is
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
        /**
         * Matches style_flat_matrix()'s pad_gap (see screen_helpers.hpp)
         * — used to compute a single panel-count button's width so the
         * Day/Night button (built first, in the Brightness row above this
         * one) can match it exactly rather than being an arbitrary
         * square. */
        constexpr uint8_t    PANEL_BTN_COUNT = 6;
        constexpr lv_coord_t PANEL_BTN_GAP   = 8;
        constexpr lv_coord_t DIVIDER2_Y      = 186;

        constexpr lv_coord_t THEME_ROW_Y          = 202;
        constexpr lv_coord_t ORIENTATION_ROW_GAP  = 16;
        /**
         * Extra breathing room after the vertical divider — without it,
         * "Orientation" sits flush against the divider line. */
        constexpr lv_coord_t ORIENTATION_LABEL_LEFT_PAD = 12;
        constexpr lv_coord_t DIVIDER3_Y           = 248;
        constexpr lv_coord_t WIRELESS_ROW_Y       = 262;
        constexpr lv_coord_t COMPACT_ROW_HEIGHT   = 38;
        constexpr lv_coord_t THEME_SWATCH_SIZE    = 22;

        /**
         * Portrait gets its own full set of row Y-positions rather than
         * reusing Landscape's — Landscape's rhythm is tuned tight to fit
         * everything in 320px of height, and just reusing it in Portrait's
         * 480px canvas left every row clustered at the top with ~250px of
         * dead space below the last row. There's enough spare height
         * (480 vs 320) that Brightness and Panels each get a label line of
         * their own above their control, instead of sharing a row with it
         * — freeing the control (slider, panel-count buttons) to use the
         * *full* row width instead of width-minus-label, and to stand
         * taller (PORTRAIT_ROW_HEIGHT, bigger than Landscape's
         * BRIGHTNESS_ROW_HEIGHT/PANELS_ROW_HEIGHT) for an easier-to-press
         * target.
         *
         * Also: Theme and Orientation can't fit as a 50/50 split at 320px
         * wide — "Orientation" alone doesn't fit a ~132px half-row — so
         * they're each a full-width row instead, stacked with a divider
         * between them (like every other row pair on this screen) rather
         * than the vertical divider Landscape uses to split one row.
         *
         * PORTRAIT_ROW_HEIGHT is shared by every control row (Brightness,
         * Panels, Theme, Orientation, Wireless) so they all read as the
         * same size — Landscape uses two different heights
         * (BRIGHTNESS_ROW_HEIGHT/PANELS_ROW_HEIGHT vs the shorter
         * COMPACT_ROW_HEIGHT for Theme/Orientation/Wireless) because it's
         * tight on space; Portrait has room for one consistent height
         * instead. */
        constexpr lv_coord_t PORTRAIT_ROW_HEIGHT              = 46;
        constexpr lv_coord_t PORTRAIT_BRIGHTNESS_LABEL_Y      = 68;
        constexpr lv_coord_t PORTRAIT_BRIGHTNESS_ROW_Y        = 94;
        constexpr lv_coord_t PORTRAIT_DIVIDER1_Y               = 154;
        constexpr lv_coord_t PORTRAIT_PANELS_LABEL_Y           = 170;
        constexpr lv_coord_t PORTRAIT_PANELS_ROW_Y             = 196;
        constexpr lv_coord_t PORTRAIT_DIVIDER2_Y               = 256;
        constexpr lv_coord_t PORTRAIT_THEME_ROW_Y              = 272;
        constexpr lv_coord_t PORTRAIT_THEME_ORIENTATION_DIVIDER_Y = 330;
        constexpr lv_coord_t PORTRAIT_ORIENTATION_ROW_Y        = 344;
        constexpr lv_coord_t PORTRAIT_DIVIDER3_Y               = 402;
        constexpr lv_coord_t PORTRAIT_WIRELESS_ROW_Y           = 416;

        constexpr lv_coord_t SCREEN_MARGIN = 20;

        constexpr int32_t BRIGHTNESS_STEP = 5;

        /**
         * A vertical counterpart to screen_helpers.hpp's create_divider() —
         * same color/thickness, just rotated, for splitting the Theme/
         * Orientation row instead of separating stacked rows. Kept local to
         * this screen rather than promoted to screen_helpers.hpp since
         * nothing else needs a vertical split yet. */
        FLASHMEM lv_obj_t *create_vertical_divider(lv_obj_t    *parent,
                                          const Theme &theme,
                                          lv_coord_t   x,
                                          lv_coord_t   y,
                                          lv_coord_t   height)
        {
            lv_obj_t *div = lv_obj_create(parent);
            lv_obj_remove_style_all(div);
            lv_obj_set_size(div, 2, height);
            lv_obj_set_style_bg_color(div, theme.palette.panel_border, 0);
            lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
            lv_obj_align(div, LV_ALIGN_TOP_LEFT, x, y);
            return div;
        }
    } // namespace

    FLASHMEM void SettingsScreen::init(const Theme &theme,
                              uint8_t      initial_day_brightness_pct,
                              uint8_t      initial_night_brightness_pct,
                              uint8_t      initial_num_panels,
                              uint32_t     initial_accent_color,
                              LayoutOrientation initial_orientation,
                              DoneCallback      done_cb,
                              void             *done_ctx,
                              PreviewBrightnessCallback preview_cb,
                              void                     *preview_ctx,
                              PickThemeCallback         pick_theme_cb,
                              void                     *pick_theme_ctx,
                              PickWirelessCallback      pick_wireless_cb,
                              void                     *pick_wireless_ctx,
                              OrientationConfirmCallback orientation_confirm_cb,
                              void                      *orientation_confirm_ctx)
    {
        theme_                = &theme;
        day_brightness_pct_   = initial_day_brightness_pct;
        night_brightness_pct_ = initial_night_brightness_pct;
        num_panels_           = initial_num_panels;
        accent_color_         = initial_accent_color;
        orientation_          = initial_orientation;
        done_cb_              = done_cb;
        done_ctx_             = done_ctx;
        preview_cb_           = preview_cb;
        preview_ctx_          = preview_ctx;
        pick_theme_cb_        = pick_theme_cb;
        pick_theme_ctx_       = pick_theme_ctx;
        pick_wireless_cb_     = pick_wireless_cb;
        pick_wireless_ctx_    = pick_wireless_ctx;
        orientation_confirm_cb_  = orientation_confirm_cb;
        orientation_confirm_ctx_ = orientation_confirm_ctx;

        screen_ = create_screen(theme);

        bool portrait = theme.metrics.screen_height > theme.metrics.screen_width;

        /**
         * Nav bar: title + close, standing in for the bottom-right
         * checkmark other screens use (see the file doc comment). */
        create_nav_title(screen_, theme, "Settings");

        DoneButton close = create_close_button(screen_, theme);
        lv_obj_add_event_cb(close.btn, done_cb_handler, LV_EVENT_CLICKED, this);

        create_divider(screen_, theme, HEADER_DIVIDER_Y);

        lv_coord_t row_width = theme.metrics.screen_width - 2 * SCREEN_MARGIN;
        /**
         * Portrait gives Brightness/Panels a label line of their own (see
         * PORTRAIT_ROW_HEIGHT's comment above) instead of sharing a row
         * with the control — freeing the control to use the full row
         * width and stand taller, for an easier-to-press target. */
        lv_coord_t control_height
            = portrait ? PORTRAIT_ROW_HEIGHT : BRIGHTNESS_ROW_HEIGHT;

        /**
         * Brightness section — Landscape: label, slider, and the Day/
         * Night toggle all share a single row (no separate "100%" readout
         * — the slider's own knob position already shows the value, and a
         * numeric readout wasn't earning its keep). Portrait: label above,
         * on its own line. */
        lv_obj_t *brightness_row = lv_obj_create(screen_);
        lv_obj_remove_style_all(brightness_row);
        lv_obj_set_size(brightness_row, row_width, control_height);
        lv_obj_align(brightness_row,
                     LV_ALIGN_TOP_LEFT,
                     SCREEN_MARGIN,
                     portrait ? PORTRAIT_BRIGHTNESS_ROW_Y : BRIGHTNESS_ROW_Y);

        lv_obj_t *brightness_label
            = lv_label_create(portrait ? screen_ : brightness_row);
        lv_label_set_text(brightness_label, "Brightness");
        lv_obj_set_style_text_color(brightness_label, theme.palette.fg, 0);
        lv_obj_set_style_text_font(
            brightness_label, theme.typography.label_lg, 0);
        if (portrait)
        {
            lv_obj_align(brightness_label,
                         LV_ALIGN_TOP_LEFT,
                         SCREEN_MARGIN,
                         PORTRAIT_BRIGHTNESS_LABEL_Y);
        }
        else
        {
            lv_obj_align(brightness_label, LV_ALIGN_LEFT_MID, 0, 0);
        }

        /**
         * Measured, not guessed — a hardcoded reserved width drifted out
         * of sync with this label's actual rendered width (a monospace
         * UI font renders wider per character than it looks like it
         * should) and let the slider start underneath the text. Only
         * needed in Landscape, where the slider shares the label's row. */
        lv_obj_update_layout(brightness_label);
        lv_coord_t brightness_label_width
            = portrait ? 0 : lv_obj_get_width(brightness_label);

        /**
         * A throwaway probe, deleted immediately after measuring — lets
         * the Day/Night button (built next) be sized to exactly match a
         * real Panels-matrix button before that matrix exists yet, without
         * duplicating PANEL_BTN_COUNT/PANEL_BTN_GAP's math against a
         * guessed label width. */
        lv_obj_t *panels_label_probe = lv_label_create(screen_);
        lv_obj_set_style_text_font(
            panels_label_probe, theme.typography.label_lg, 0);
        lv_label_set_text(panels_label_probe, "Panels");
        lv_obj_update_layout(panels_label_probe);
        /**
         * Landscape: Panels' row is label + matrix side by side, so the
         * matrix only gets what's left after the label. Portrait: the
         * label's on its own line above, so the matrix (and to match it,
         * the Day/Night button below) gets the full row width. */
        lv_coord_t panels_matrix_width
            = portrait ? row_width
                       : row_width - lv_obj_get_width(panels_label_probe)
                             - PANELS_LABEL_GAP;
        lv_obj_delete(panels_label_probe);
        lv_coord_t panel_btn_width
            = (panels_matrix_width - (PANEL_BTN_COUNT - 1) * PANEL_BTN_GAP)
              / PANEL_BTN_COUNT;

        /**
         * Day/Night is a fast manual switch, not automatic (no clock/
         * light-sensor wiring yet) — flipping it immediately re-applies
         * the real backlight via preview_cb_ so it's actually useful
         * mid-drive, not just a setting you'd tune once and forget. A
         * single button whose glyph swaps between sun/moon
         * (theme.typography.icon_day_night) on each tap, rather than a
         * two-button matrix — one button that toggles reads more like a
         * standard day/night switch than two icon buttons side by side.
         * Landscape: sized to match a single Panels-matrix button exactly,
         * so the two rows' rightmost controls read as the same control
         * size. Portrait: square (matching the row height) instead —
         * Panels' buttons already got bigger from using the full row
         * width, so matching them isn't needed to feel consistent the way
         * it is in Landscape, and a square reads more like a toggle than
         * a stretched rectangle would. */
        lv_coord_t day_night_btn_width
            = portrait ? control_height : panel_btn_width;
        day_night_btn_ = lv_btn_create(brightness_row);
        lv_obj_set_size(day_night_btn_, day_night_btn_width, control_height);
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

        /**
         * Slider fills whatever's left between the label's actual width
         * (Landscape only — Portrait's label isn't in this row) and the
         * Day/Night button. */
        lv_coord_t slider_width
            = portrait ? row_width - day_night_btn_width
                             - BRIGHTNESS_SLIDER_BTN_GAP
                       : row_width - brightness_label_width
                             - BRIGHTNESS_LABEL_GAP - panel_btn_width
                             - BRIGHTNESS_SLIDER_BTN_GAP;

        brightness_slider_ = lv_slider_create(brightness_row);
        lv_obj_set_width(brightness_slider_, slider_width);
        lv_obj_align(
            brightness_slider_,
            LV_ALIGN_LEFT_MID,
            portrait ? 0 : brightness_label_width + BRIGHTNESS_LABEL_GAP,
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

        create_divider(
            screen_, theme, portrait ? PORTRAIT_DIVIDER1_Y : DIVIDER1_Y);

        /**
         * Panels section — same Landscape-single-row / Portrait-label-
         * above shape as Brightness above it. */
        lv_obj_t *panels_row = lv_obj_create(screen_);
        lv_obj_remove_style_all(panels_row);
        lv_obj_set_size(panels_row, row_width, control_height);
        lv_obj_align(panels_row,
                     LV_ALIGN_TOP_LEFT,
                     SCREEN_MARGIN,
                     portrait ? PORTRAIT_PANELS_ROW_Y : PANELS_ROW_Y);

        lv_obj_t *panel_count_label
            = lv_label_create(portrait ? screen_ : panels_row);
        lv_obj_set_style_text_font(
            panel_count_label, theme.typography.label_lg, 0);
        lv_obj_set_style_text_color(panel_count_label, theme.palette.fg, 0);
        lv_label_set_text(panel_count_label, "Panels");
        if (portrait)
        {
            lv_obj_align(panel_count_label,
                         LV_ALIGN_TOP_LEFT,
                         SCREEN_MARGIN,
                         PORTRAIT_PANELS_LABEL_Y);
        }
        else
        {
            lv_obj_align(panel_count_label, LV_ALIGN_LEFT_MID, 0, 0);
        }

        /**
         * A row of buttons instead of a vertical roller — the roller's 3
         * visible rows stood tall enough to cover the "Panels" label
         * above it; this is a fixed, compact height regardless of how
         * many options there are. */
        static const char *const panel_count_map[]
            = { "1", "2", "3", "4", "5", "6", "" };
        panel_count_matrix_ = lv_buttonmatrix_create(panels_row);
        lv_obj_set_style_text_font(panel_count_matrix_, theme.typography.ui, 0);
        lv_obj_set_size(
            panel_count_matrix_, panels_matrix_width, control_height);
        lv_obj_align(panel_count_matrix_,
                     portrait ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID,
                     0,
                     0);
        lv_buttonmatrix_set_map(panel_count_matrix_, panel_count_map);
        lv_buttonmatrix_set_one_checked(panel_count_matrix_, true);
        lv_buttonmatrix_set_button_ctrl_all(panel_count_matrix_,
                                            LV_BUTTONMATRIX_CTRL_CHECKABLE);
        style_flat_matrix(panel_count_matrix_, theme, MATRIX_ITEM_PAD_KEEP);

        create_divider(
            screen_, theme, portrait ? PORTRAIT_DIVIDER2_Y : DIVIDER2_Y);

        /**
         * Theme + Orientation share one row, 50/50, in Landscape — Theme
         * doesn't need a full-width row to itself, and this screen is
         * tight on vertical space (see the file doc comment). Portrait
         * doesn't have the width to spare for that split (see
         * PORTRAIT_ORIENTATION_ROW_Y's comment above), so the two become
         * separate full-width rows instead, using some of Portrait's
         * extra height. */
        lv_coord_t half_row_width
            = (theme.metrics.screen_width - 2 * SCREEN_MARGIN
               - ORIENTATION_ROW_GAP)
              / 2;
        lv_coord_t theme_row_width = portrait
                                         ? theme.metrics.screen_width
                                               - 2 * SCREEN_MARGIN
                                         : half_row_width;

        if (!portrait)
        {
            create_vertical_divider(
                screen_,
                theme,
                SCREEN_MARGIN + half_row_width + ORIENTATION_ROW_GAP / 2 - 1,
                THEME_ROW_Y,
                COMPACT_ROW_HEIGHT);
        }

        /**
         * Theme row: tappable, hands off to ThemePickerScreen rather than
         * embedding a color grid here — same reasoning as Configurator's
         * Signal/Warning Thresholds rows. */
        lv_coord_t compact_row_height
            = portrait ? PORTRAIT_ROW_HEIGHT : COMPACT_ROW_HEIGHT;

        ListRow theme_row = create_list_row(screen_, theme, "Theme");
        lv_obj_set_size(theme_row.btn, theme_row_width, compact_row_height);
        lv_obj_align(theme_row.btn,
                     LV_ALIGN_TOP_LEFT,
                     SCREEN_MARGIN,
                     portrait ? PORTRAIT_THEME_ROW_Y : THEME_ROW_Y);
        lv_obj_add_event_cb(
            theme_row.btn, pick_theme_cb_handler, LV_EVENT_CLICKED, this);

        /**
         * Current-accent preview — a plain colored square rather than a
         * text swatch name, since the presets don't have user-facing
         * names beyond their appearance. */
        theme_swatch_ = lv_obj_create(theme_row.btn);
        lv_obj_set_size(theme_swatch_, THEME_SWATCH_SIZE, THEME_SWATCH_SIZE);
        lv_obj_set_style_radius(theme_swatch_, 6, 0);
        lv_obj_set_style_border_width(
            theme_swatch_, theme.metrics.panel_border_width, 0);
        lv_obj_set_style_border_color(
            theme_swatch_, theme.palette.panel_border, 0);
        lv_obj_set_style_shadow_width(theme_swatch_, 0, 0);
        lv_obj_align_to(
            theme_swatch_, theme_row.chevron, LV_ALIGN_OUT_LEFT_MID, -12, 0);

        /**
         * Orientation row: a label plus a compact toggle button (matching
         * the Day/Night button's styling), not a navigation row — there's
         * nowhere to hand off to, the button IS the control. A plain tap
         * opens the confirm/cancel popup below rather than applying the
         * flip immediately — that popup is what guards against a stray
         * tap actually changing anything, so the button itself doesn't
         * need a long-press to do the same job twice. */
        if (portrait)
        {
            create_divider(
                screen_, theme, PORTRAIT_THEME_ORIENTATION_DIVIDER_Y);
        }

        lv_obj_t *orientation_row = lv_obj_create(screen_);
        lv_obj_remove_style_all(orientation_row);
        lv_obj_set_size(orientation_row,
                        portrait ? theme_row_width : half_row_width,
                        compact_row_height);
        lv_obj_align(orientation_row,
                     LV_ALIGN_TOP_LEFT,
                     portrait ? SCREEN_MARGIN
                              : SCREEN_MARGIN + half_row_width
                                    + ORIENTATION_ROW_GAP,
                     portrait ? PORTRAIT_ORIENTATION_ROW_Y : THEME_ROW_Y);

        lv_obj_t *orientation_label = lv_label_create(orientation_row);
        lv_label_set_text(orientation_label, "Orientation");
        lv_obj_set_style_text_color(orientation_label, theme.palette.fg, 0);
        lv_obj_set_style_text_font(
            orientation_label, theme.typography.label_lg, 0);
        /**
         * In Portrait this row is full-width like Theme's, so it matches
         * Theme's flush-left label (no left pad needed). Landscape's pad
         * is still needed there — see ORIENTATION_LABEL_LEFT_PAD's
         * comment above. */
        lv_obj_align(orientation_label,
                     LV_ALIGN_LEFT_MID,
                     portrait ? 0 : ORIENTATION_LABEL_LEFT_PAD,
                     0);

        orientation_btn_ = lv_btn_create(orientation_row);
        lv_obj_set_size(orientation_btn_, compact_row_height, compact_row_height);
        lv_obj_align(orientation_btn_, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(orientation_btn_, theme.palette.bg, 0);
        lv_obj_set_style_bg_opa(orientation_btn_, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(orientation_btn_, 8, 0);
        lv_obj_set_style_border_width(
            orientation_btn_, theme.metrics.panel_border_width, 0);
        lv_obj_set_style_border_color(
            orientation_btn_, theme.palette.panel_border, 0);
        lv_obj_set_style_shadow_width(orientation_btn_, 0, 0);
        lv_obj_set_style_bg_color(
            orientation_btn_, theme.palette.accent, LV_STATE_PRESSED);
        lv_obj_add_event_cb(
            orientation_btn_, orientation_clicked_cb, LV_EVENT_CLICKED, this);

        orientation_lbl_ = lv_label_create(orientation_btn_);
        lv_obj_set_style_text_font(orientation_lbl_, theme.typography.title, 0);
        lv_obj_set_style_text_color(orientation_lbl_, theme.palette.fg, 0);
        lv_obj_center(orientation_lbl_);

        create_divider(
            screen_, theme, portrait ? PORTRAIT_DIVIDER3_Y : DIVIDER3_Y);

        /**
         * WiFi row: tappable, hands off to WirelessSetupScreen — same
         * shape as the Theme row above it, just without a preview swatch
         * (there's no "current value" to show at a glance). Full width:
         * logging used to share this row, but it moved to its own
         * LoggingScreen (swipe up from InfoScreen) once it grew past what
         * a checkbox could usefully say. */
        ListRow wireless_row = create_list_row(screen_, theme, "WiFi");
        lv_obj_set_size(wireless_row.btn,
                        theme.metrics.screen_width - 40,
                        compact_row_height);
        lv_obj_align(wireless_row.btn,
                     LV_ALIGN_TOP_MID,
                     0,
                     portrait ? PORTRAIT_WIRELESS_ROW_Y : WIRELESS_ROW_Y);
        lv_obj_add_event_cb(
            wireless_row.btn, pick_wireless_cb_handler, LV_EVENT_CLICKED, this);
    }

    void SettingsScreen::open()
    {
        /**
         * Reopens showing whichever of day/night was last selected, not
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
        refresh_orientation_label();

        lv_screen_load_anim(
            screen_, LV_SCR_LOAD_ANIM_OVER_BOTTOM, 220, 0, false);
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

    void SettingsScreen::refresh_orientation_label()
    {
        lv_label_set_text(orientation_lbl_,
                          orientation_ == LayoutOrientation::Horizontal ? "H"
                                                                        : "V");
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

        /**
         * lv_slider has no native step option — snap to the nearest 5%
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

    void SettingsScreen::orientation_clicked_cb(lv_event_t *e)
    {
        auto *self = static_cast<SettingsScreen *>(lv_event_get_user_data(e));
        const Theme &theme = *self->theme_;

        self->pending_orientation_
            = self->orientation_ == LayoutOrientation::Horizontal
                  ? LayoutOrientation::Vertical
                  : LayoutOrientation::Horizontal;

        /**
         * Rotating the panel only takes effect on the next boot (see
         * platform.hpp's DisplayOrientation) -- a silent flip with no
         * visible effect until some future restart would look broken, so
         * this asks first rather than applying it immediately. */
        ConfirmPopup popup = create_confirm_popup(
            theme,
            self->pending_orientation_ == LayoutOrientation::Vertical
                ? "Switch to Vertical?"
                : "Switch to Horizontal?",
            "Restart required to apply.",
            "Confirm");

        lv_obj_add_event_cb(
            popup.confirm_btn, orientation_confirm_cb, LV_EVENT_CLICKED, self);
        lv_obj_add_event_cb(
            popup.cancel_btn, orientation_cancel_cb, LV_EVENT_CLICKED, self);

        self->orientation_confirm_mbox_ = popup.mbox;
    }

    void SettingsScreen::orientation_confirm_cb(lv_event_t *e)
    {
        auto *self = static_cast<SettingsScreen *>(lv_event_get_user_data(e));
        self->orientation_ = self->pending_orientation_;
        self->refresh_orientation_label();
        lv_msgbox_close_async(self->orientation_confirm_mbox_);
        self->orientation_confirm_mbox_ = nullptr;

        /**
         * Persisted right here, not deferred to Done — see the file doc
         * comment for why. */
        if (self->orientation_confirm_cb_ != nullptr)
        {
            self->orientation_confirm_cb_(self->orientation_confirm_ctx_,
                                          self->orientation_);
        }
    }

    void SettingsScreen::orientation_cancel_cb(lv_event_t *e)
    {
        auto *self = static_cast<SettingsScreen *>(lv_event_get_user_data(e));
        lv_msgbox_close_async(self->orientation_confirm_mbox_);
        self->orientation_confirm_mbox_ = nullptr;
    }

    void SettingsScreen::done_cb_handler(lv_event_t *e)
    {
        auto *self = static_cast<SettingsScreen *>(lv_event_get_user_data(e));

        self->capture_slider_value();

        /**
         * get_selected_button() tracks focus/last-press, not the checked
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
