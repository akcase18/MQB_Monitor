#include "configurator_screen.hpp"

#include <stdio.h>

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        constexpr lv_coord_t ROW_HEIGHT = 36;

        /* A compact settings-list layout, not a set of stretched equal-
         * height sections like WarningConfigScreen/SettingsScreen — every
         * row here is a single line, so there's no natural content to
         * spread across extra vertical space the way a slider or a
         * roller would use it. */
        constexpr lv_coord_t HEADER_DIVIDER_Y = 50;
        constexpr lv_coord_t SIGNAL_ROW_Y     = 64;
        constexpr lv_coord_t DIVIDER1_Y       = 110;
        constexpr lv_coord_t UNITS_ROW_Y      = 124;
        constexpr lv_coord_t DIVIDER2_Y       = 170;
        constexpr lv_coord_t DISPLAY_ROW_Y    = 184;
        constexpr lv_coord_t DIVIDER3_Y       = 230;
        constexpr lv_coord_t WARN_ROW_Y       = 244;

        /* Restyles a checkable lv_buttonmatrix away from LVGL's default
         * light-mode card+shadow look to match the flat, bordered,
         * accent-on-checked treatment used everywhere else in this app. */
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
            lv_obj_set_style_pad_all(matrix, 8, LV_PART_ITEMS);
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

    void ConfiguratorScreen::init(const Theme             &theme,
                                  const char *const *const signal_names,
                                  const uint16_t          *signal_ids,
                                  uint16_t                 num_signals,
                                  DoneCallback             done_cb,
                                  void                    *done_ctx,
                                  SettingsCallback         settings_cb,
                                  void                    *settings_ctx,
                                  PickSignalCallback       pick_signal_cb,
                                  void                    *pick_signal_ctx)
    {
        signal_names_    = signal_names;
        signal_ids_      = signal_ids;
        num_signals_     = num_signals;
        done_cb_         = done_cb;
        done_ctx_        = done_ctx;
        settings_cb_     = settings_cb;
        settings_ctx_    = settings_ctx;
        pick_signal_cb_  = pick_signal_cb;
        pick_signal_ctx_ = pick_signal_ctx;

        screen_ = create_screen(theme);

        /* Nav bar: title + close, standing in for the bottom-right
         * checkmark other screens use (see the file doc comment). */
        lv_obj_t *title_lbl = lv_label_create(screen_);
        lv_label_set_text(title_lbl, "Change Signal");
        lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 20, 12);
        lv_obj_set_style_text_color(title_lbl, theme.palette.fg, 0);
        lv_obj_set_style_text_font(title_lbl, theme.typography.title, 0);

        DoneButton close = create_close_button(screen_, theme);
        lv_obj_add_event_cb(close.btn, done_cb_handler, LV_EVENT_CLICKED, this);

        create_divider(screen_, theme, HEADER_DIVIDER_Y);

        /* Signal row: tappable, hands off to SignalPickerScreen rather
         * than embedding a roller here (see the file doc comment). */
        lv_obj_t *signal_row_btn = lv_btn_create(screen_);
        lv_obj_set_size(
            signal_row_btn, theme.metrics.screen_width - 40, ROW_HEIGHT);
        lv_obj_align(signal_row_btn, LV_ALIGN_TOP_MID, 0, SIGNAL_ROW_Y);
        lv_obj_set_style_bg_opa(signal_row_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(signal_row_btn, 0, 0);
        lv_obj_set_style_shadow_width(signal_row_btn, 0, 0);
        /* lv_btn's default theme pads its content in from the button's own
         * edges — without zeroing that, LEFT_MID-aligned children sit
         * further right than the plain labels elsewhere on this screen
         * ("Change Signal", "Units"), which aren't buttons and so have no
         * such padding. */
        lv_obj_set_style_pad_all(signal_row_btn, 0, 0);
        lv_obj_set_style_bg_color(
            signal_row_btn, theme.palette.accent, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(signal_row_btn, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_add_event_cb(
            signal_row_btn, pick_signal_cb_handler, LV_EVENT_CLICKED, this);

        lv_obj_t *signal_label = lv_label_create(signal_row_btn);
        lv_label_set_text(signal_label, "Signal");
        lv_obj_set_style_text_color(signal_label, theme.palette.fg, 0);
        lv_obj_set_style_text_font(signal_label, theme.typography.label_lg, 0);
        lv_obj_align(signal_label, LV_ALIGN_LEFT_MID, 0, 0);

        signal_chevron_ = lv_label_create(signal_row_btn);
        lv_obj_set_style_text_font(
            signal_chevron_, theme.typography.symbol_lg, 0);
        lv_label_set_text(signal_chevron_, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(signal_chevron_, theme.palette.fg, 0);
        lv_obj_align(signal_chevron_, LV_ALIGN_RIGHT_MID, 0, 0);

        /* Text (and therefore width) isn't known until refresh_signal_row_
         * label() sets it in open()/set_selected_signal() — aligning here
         * with the label still empty bakes in a position that's wrong
         * once real text arrives, so alignment is redone there instead of
         * once here (see refresh_signal_row_label()). */
        signal_value_lbl_ = lv_label_create(signal_row_btn);
        /* Slightly darkened, not the plain fg every other label on this
         * screen uses — sets the current value apart from the row's
         * static "Signal" label at a glance. */
        lv_obj_set_style_text_color(
            signal_value_lbl_, lv_color_darken(theme.palette.fg, 60), 0);
        lv_obj_set_style_text_font(
            signal_value_lbl_, theme.typography.label_lg, 0);

        create_divider(screen_, theme, DIVIDER1_Y);

        /* Units section — a single row, label left / segmented control
         * right, rather than a second stacked sub-row: Imperial vs Metric
         * doesn't need the extra room a +/- control would. Label and
         * matrix are both LEFT_MID/RIGHT_MID children of one row
         * container so they vertically center against each other
         * regardless of font size or matrix height — a fixed TOP_LEFT
         * label offset tuned for one font looked visibly off-center
         * against the (taller) matrix once the label's font changed. */
        lv_obj_t *units_row = lv_obj_create(screen_);
        lv_obj_remove_style_all(units_row);
        lv_obj_set_size(
            units_row, theme.metrics.screen_width - 40, ROW_HEIGHT + 8);
        lv_obj_align(units_row, LV_ALIGN_TOP_MID, 0, UNITS_ROW_Y - 6);

        lv_obj_t *units_label = lv_label_create(units_row);
        lv_label_set_text(units_label, "Units");
        lv_obj_set_style_text_color(units_label, theme.palette.fg, 0);
        lv_obj_set_style_text_font(units_label, theme.typography.label_lg, 0);
        lv_obj_align(units_label, LV_ALIGN_LEFT_MID, 0, 0);

        static const char *const units_map[] = { "Imperial", "Metric", "" };
        units_matrix_ = lv_buttonmatrix_create(units_row);
        lv_obj_set_style_text_font(units_matrix_, theme.typography.label_lg, 0);
        lv_obj_set_size(units_matrix_, 240, ROW_HEIGHT + 8);
        lv_obj_align(units_matrix_, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_buttonmatrix_set_map(units_matrix_, units_map);
        lv_buttonmatrix_set_one_checked(units_matrix_, true);
        lv_buttonmatrix_set_button_ctrl_all(units_matrix_,
                                            LV_BUTTONMATRIX_CTRL_CHECKABLE);
        style_flat_matrix(units_matrix_, theme);

        create_divider(screen_, theme, DIVIDER2_Y);

        /* Display section — same single-row shape as Units, just with a
         * 3rd segment. */
        lv_obj_t *display_row = lv_obj_create(screen_);
        lv_obj_remove_style_all(display_row);
        lv_obj_set_size(
            display_row, theme.metrics.screen_width - 40, ROW_HEIGHT + 8);
        lv_obj_align(display_row, LV_ALIGN_TOP_MID, 0, DISPLAY_ROW_Y - 6);

        lv_obj_t *display_label = lv_label_create(display_row);
        /* "Mode", not "Display" — the shorter label leaves the 3-segment
         * matrix room for the same per-button breathing room the 2-segment
         * Units matrix above it has ("Digital" is nearly as wide as
         * "Imperial", but there's a 3rd segment to fit in the same row). */
        lv_label_set_text(display_label, "Mode");
        lv_obj_set_style_text_color(display_label, theme.palette.fg, 0);
        lv_obj_set_style_text_font(display_label, theme.typography.label_lg, 0);
        lv_obj_align(display_label, LV_ALIGN_LEFT_MID, 0, 0);

        static const char *const mode_map[] = { "Digital", "Bar", "Gauge", "" };
        mode_matrix_ = lv_buttonmatrix_create(display_row);
        lv_obj_set_style_text_font(mode_matrix_, theme.typography.label_lg, 0);
        lv_obj_set_size(mode_matrix_, 360, ROW_HEIGHT + 8);
        lv_obj_align(mode_matrix_, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_buttonmatrix_set_map(mode_matrix_, mode_map);
        lv_buttonmatrix_set_one_checked(mode_matrix_, true);
        lv_buttonmatrix_set_button_ctrl_all(mode_matrix_,
                                            LV_BUTTONMATRIX_CTRL_CHECKABLE);
        style_flat_matrix(mode_matrix_, theme);

        create_divider(screen_, theme, DIVIDER3_Y);

        /* Warning Thresholds row: the whole row is the tap target (not
         * just a small corner icon), label left / chevron right — a
         * settings-list-style row instead of a floating gear button. */
        lv_obj_t *warn_row_btn = lv_btn_create(screen_);
        lv_obj_set_size(
            warn_row_btn, theme.metrics.screen_width - 40, ROW_HEIGHT);
        lv_obj_align(warn_row_btn, LV_ALIGN_TOP_MID, 0, WARN_ROW_Y);
        lv_obj_set_style_bg_opa(warn_row_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(warn_row_btn, 0, 0);
        lv_obj_set_style_shadow_width(warn_row_btn, 0, 0);
        lv_obj_set_style_pad_all(warn_row_btn, 0, 0);
        lv_obj_set_style_bg_color(
            warn_row_btn, theme.palette.accent, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(warn_row_btn, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_add_event_cb(
            warn_row_btn, settings_cb_handler, LV_EVENT_CLICKED, this);

        lv_obj_t *warn_label = lv_label_create(warn_row_btn);
        lv_label_set_text(warn_label, "Warning Thresholds");
        lv_obj_set_style_text_color(warn_label, theme.palette.fg, 0);
        lv_obj_set_style_text_font(warn_label, theme.typography.label_lg, 0);
        lv_obj_align(warn_label, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *warn_chevron = lv_label_create(warn_row_btn);
        lv_obj_set_style_text_font(warn_chevron, theme.typography.symbol_lg, 0);
        lv_label_set_text(warn_chevron, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(warn_chevron, theme.palette.fg, 0);
        lv_obj_align(warn_chevron, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    void ConfiguratorScreen::open(uint8_t     rank,
                                  uint16_t    signal_id,
                                  bool        is_metric,
                                  DisplayMode mode)
    {
        current_rank_      = rank;
        opened_signal_id_  = signal_id;
        pending_signal_id_ = signal_id;
        refresh_signal_row_label();

        /* units_map is { "Imperial", "Metric" } — index 1 is Metric. */
        lv_buttonmatrix_set_button_ctrl(
            units_matrix_, is_metric ? 1 : 0, LV_BUTTONMATRIX_CTRL_CHECKED);

        /* mode_map is { "Digital", "Bar", "Gauge" }. */
        lv_buttonmatrix_set_button_ctrl(mode_matrix_,
                                        static_cast<uint16_t>(mode),
                                        LV_BUTTONMATRIX_CTRL_CHECKED);

        lv_screen_load(screen_);
    }

    void ConfiguratorScreen::set_selected_signal(uint16_t signal_id)
    {
        pending_signal_id_ = signal_id;
        refresh_signal_row_label();
    }

    void ConfiguratorScreen::refresh_signal_row_label()
    {
        const char *name = "";
        for (uint16_t i = 0; i < num_signals_; i++)
        {
            if (signal_ids_[i] == pending_signal_id_)
            {
                name = signal_names_[i];
                break;
            }
        }
        lv_label_set_text(signal_value_lbl_, name);

        /* Re-align every time, not just once at creation — the label's
         * width depends on its text, so a stale alignment from a shorter
         * (or empty) previous string would leave it overlapping
         * signal_chevron_ once a longer name is set (see init()). */
        lv_obj_align_to(
            signal_value_lbl_, signal_chevron_, LV_ALIGN_OUT_LEFT_MID, -8, 0);
    }

    void ConfiguratorScreen::done_cb_handler(lv_event_t *e)
    {
        auto *self
            = static_cast<ConfiguratorScreen *>(lv_event_get_user_data(e));
        if (self->done_cb_ == nullptr)
        {
            return;
        }

        /* has_button_ctrl scans for the actually-checked button rather than
         * trusting get_selected_button(), which only tracks focus/last-press
         * — same reasoning as SettingsScreen's panel-count matrix. */
        bool is_metric = lv_buttonmatrix_has_button_ctrl(
            self->units_matrix_, 1, LV_BUTTONMATRIX_CTRL_CHECKED);

        DisplayMode mode = DisplayMode::Digital;
        for (uint16_t i = 0; i < 3; i++)
        {
            if (lv_buttonmatrix_has_button_ctrl(
                    self->mode_matrix_, i, LV_BUTTONMATRIX_CTRL_CHECKED))
            {
                mode = static_cast<DisplayMode>(i);
                break;
            }
        }

        self->done_cb_(self->done_ctx_,
                       self->current_rank_,
                       self->pending_signal_id_,
                       is_metric,
                       mode);
    }

    void ConfiguratorScreen::settings_cb_handler(lv_event_t *e)
    {
        auto *self
            = static_cast<ConfiguratorScreen *>(lv_event_get_user_data(e));
        if (self->settings_cb_ != nullptr)
        {
            self->settings_cb_(self->settings_ctx_,
                               self->current_rank_,
                               self->pending_signal_id_);
        }
    }

    void ConfiguratorScreen::pick_signal_cb_handler(lv_event_t *e)
    {
        auto *self
            = static_cast<ConfiguratorScreen *>(lv_event_get_user_data(e));
        if (self->pick_signal_cb_ != nullptr)
        {
            self->pick_signal_cb_(self->pick_signal_ctx_,
                                  self->pending_signal_id_);
        }
    }

} // namespace gui
