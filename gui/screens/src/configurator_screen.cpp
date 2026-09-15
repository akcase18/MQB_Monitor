#include "configurator_screen.hpp"
#include "mem_placement.hpp"

#include <stdio.h>

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        constexpr lv_coord_t ROW_HEIGHT = 36;

        /**
         * A compact settings-list layout, not a set of stretched equal-
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

        /**
         * Portrait (320 wide) doesn't have the width for units_matrix_
         * (240px) or mode_matrix_ (360px, wider than the whole row) to sit
         * beside their row's label the way Landscape does -- mode_matrix_
         * would overflow off the left edge entirely. Portrait has plenty
         * of *height* to spare (480 vs 320) instead, so those two rows
         * stack their label above a full-row-width matrix rather than
         * shrinking the matrix to fit -- and since that height is going
         * spare anyway, the matrix itself gets taller too
         * (PORTRAIT_MATRIX_HEIGHT, vs Landscape's ROW_HEIGHT+8), rather
         * than reusing Landscape's compact button height and just leaving
         * the extra room empty below Warning Thresholds. */
        constexpr lv_coord_t PORTRAIT_UNITS_LABEL_Y  = 124;
        constexpr lv_coord_t PORTRAIT_UNITS_MATRIX_Y = 154;
        constexpr lv_coord_t PORTRAIT_MATRIX_HEIGHT   = 48;
        constexpr lv_coord_t PORTRAIT_DIVIDER2_Y      = 232;
        constexpr lv_coord_t PORTRAIT_DISPLAY_LABEL_Y  = 246;
        constexpr lv_coord_t PORTRAIT_DISPLAY_MATRIX_Y = 276;
        constexpr lv_coord_t PORTRAIT_DIVIDER3_Y       = 354;
        constexpr lv_coord_t PORTRAIT_WARN_ROW_Y       = 368;

        /**
         * ConfiguratorScreen's matrices carry word labels ("Imperial",
         * "Digital"), so they set an explicit item padding rather than
         * keeping LVGL's default -- see screen_helpers.hpp's
         * style_flat_matrix(). */
        constexpr lv_coord_t MATRIX_ITEM_PAD = 8;
    } // namespace

    FLASHMEM void ConfiguratorScreen::init(const Theme             &theme,
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

        /**
         * Nav bar: title + close, standing in for the bottom-right
         * checkmark other screens use (see the file doc comment). */
        create_nav_title(screen_, theme, "Change Signal");

        DoneButton close = create_close_button(screen_, theme);
        lv_obj_add_event_cb(close.btn, done_cb_handler, LV_EVENT_CLICKED, this);

        create_divider(screen_, theme, HEADER_DIVIDER_Y);

        /**
         * Signal row: tappable, hands off to SignalPickerScreen rather
         * than embedding a roller here (see the file doc comment). */
        ListRow signal_row = create_list_row(screen_, theme, "Signal");
        lv_obj_set_size(
            signal_row.btn, theme.metrics.screen_width - 40, ROW_HEIGHT);
        lv_obj_align(signal_row.btn, LV_ALIGN_TOP_MID, 0, SIGNAL_ROW_Y);
        lv_obj_add_event_cb(
            signal_row.btn, pick_signal_cb_handler, LV_EVENT_CLICKED, this);
        signal_chevron_ = signal_row.chevron;

        /**
         * Text (and therefore width) isn't known until refresh_signal_row_
         * label() sets it in open()/set_selected_signal() — aligning here
         * with the label still empty bakes in a position that's wrong
         * once real text arrives, so alignment is redone there instead of
         * once here (see refresh_signal_row_label()). */
        signal_value_lbl_ = lv_label_create(signal_row.btn);
        /**
         * Slightly darkened, not the plain fg every other label on this
         * screen uses — sets the current value apart from the row's
         * static "Signal" label at a glance. */
        lv_obj_set_style_text_color(
            signal_value_lbl_, lv_color_darken(theme.palette.fg, 60), 0);
        lv_obj_set_style_text_font(
            signal_value_lbl_, theme.typography.label_lg, 0);

        create_divider(screen_, theme, DIVIDER1_Y);

        bool portrait = theme.metrics.screen_height > theme.metrics.screen_width;
        lv_coord_t row_width = theme.metrics.screen_width - 40;

        /**
         * Units section — Landscape: a single row, label left / segmented
         * control right, rather than a second stacked sub-row (Imperial
         * vs Metric doesn't need the extra room a +/- control would).
         * Portrait: label above a full-row-width matrix instead (see
         * PORTRAIT_UNITS_LABEL_Y's comment above) — units_matrix_'s fixed
         * 240px width doesn't leave enough of a 280px-ish portrait row for
         * the label beside it. Label and matrix are both LEFT_MID/
         * RIGHT_MID (or TOP_LEFT/below, in Portrait) children of one row
         * container so they vertically center against each other
         * regardless of font size or matrix height — a fixed TOP_LEFT
         * label offset tuned for one font looked visibly off-center
         * against the (taller) matrix once the label's font changed. */
        lv_obj_t *units_row = lv_obj_create(screen_);
        lv_obj_remove_style_all(units_row);
        lv_obj_set_size(units_row,
                        row_width,
                        portrait ? (PORTRAIT_DIVIDER2_Y - PORTRAIT_UNITS_LABEL_Y)
                                 : ROW_HEIGHT + 8);
        lv_obj_align(units_row,
                     LV_ALIGN_TOP_MID,
                     0,
                     portrait ? PORTRAIT_UNITS_LABEL_Y : UNITS_ROW_Y - 6);

        lv_obj_t *units_label = lv_label_create(units_row);
        lv_label_set_text(units_label, "Units");
        lv_obj_set_style_text_color(units_label, theme.palette.fg, 0);
        lv_obj_set_style_text_font(units_label, theme.typography.label_lg, 0);
        lv_obj_align(units_label,
                     portrait ? LV_ALIGN_TOP_LEFT : LV_ALIGN_LEFT_MID,
                     0,
                     0);

        static const char *const units_map[] = { "Imperial", "Metric", "" };
        units_matrix_ = lv_buttonmatrix_create(units_row);
        lv_obj_set_style_text_font(units_matrix_, theme.typography.label_lg, 0);
        lv_obj_set_size(units_matrix_,
                        portrait ? row_width : 240,
                        portrait ? PORTRAIT_MATRIX_HEIGHT : ROW_HEIGHT + 8);
        lv_obj_align(units_matrix_,
                     portrait ? LV_ALIGN_TOP_LEFT : LV_ALIGN_RIGHT_MID,
                     0,
                     portrait ? PORTRAIT_UNITS_MATRIX_Y - PORTRAIT_UNITS_LABEL_Y
                              : 0);
        lv_buttonmatrix_set_map(units_matrix_, units_map);
        lv_buttonmatrix_set_one_checked(units_matrix_, true);
        lv_buttonmatrix_set_button_ctrl_all(units_matrix_,
                                            LV_BUTTONMATRIX_CTRL_CHECKABLE);
        style_flat_matrix(units_matrix_, theme, MATRIX_ITEM_PAD);

        create_divider(
            screen_, theme, portrait ? PORTRAIT_DIVIDER2_Y : DIVIDER2_Y);

        /**
         * Display section — same shape as Units above (single row in
         * Landscape, stacked in Portrait), just with a 3rd segment. */
        lv_obj_t *display_row = lv_obj_create(screen_);
        lv_obj_remove_style_all(display_row);
        lv_obj_set_size(
            display_row,
            row_width,
            portrait ? (PORTRAIT_DIVIDER3_Y - PORTRAIT_DISPLAY_LABEL_Y)
                     : ROW_HEIGHT + 8);
        lv_obj_align(display_row,
                     LV_ALIGN_TOP_MID,
                     0,
                     portrait ? PORTRAIT_DISPLAY_LABEL_Y : DISPLAY_ROW_Y - 6);

        lv_obj_t *display_label = lv_label_create(display_row);
        /**
         * "Mode", not "Display" — the shorter label leaves the 3-segment
         * matrix room for the same per-button breathing room the 2-segment
         * Units matrix above it has ("Digital" is nearly as wide as
         * "Imperial", but there's a 3rd segment to fit in the same row). */
        lv_label_set_text(display_label, "Mode");
        lv_obj_set_style_text_color(display_label, theme.palette.fg, 0);
        lv_obj_set_style_text_font(display_label, theme.typography.label_lg, 0);
        lv_obj_align(display_label,
                     portrait ? LV_ALIGN_TOP_LEFT : LV_ALIGN_LEFT_MID,
                     0,
                     0);

        static const char *const mode_map[] = { "Digital", "Bar", "Gauge", "" };
        mode_matrix_ = lv_buttonmatrix_create(display_row);
        lv_obj_set_style_text_font(mode_matrix_, theme.typography.label_lg, 0);
        lv_obj_set_size(mode_matrix_,
                        portrait ? row_width : 360,
                        portrait ? PORTRAIT_MATRIX_HEIGHT : ROW_HEIGHT + 8);
        lv_obj_align(
            mode_matrix_,
            portrait ? LV_ALIGN_TOP_LEFT : LV_ALIGN_RIGHT_MID,
            0,
            portrait ? PORTRAIT_DISPLAY_MATRIX_Y - PORTRAIT_DISPLAY_LABEL_Y : 0);
        lv_buttonmatrix_set_map(mode_matrix_, mode_map);
        lv_buttonmatrix_set_one_checked(mode_matrix_, true);
        lv_buttonmatrix_set_button_ctrl_all(mode_matrix_,
                                            LV_BUTTONMATRIX_CTRL_CHECKABLE);
        style_flat_matrix(mode_matrix_, theme, MATRIX_ITEM_PAD);

        create_divider(
            screen_, theme, portrait ? PORTRAIT_DIVIDER3_Y : DIVIDER3_Y);

        /**
         * Warning Thresholds row: the whole row is the tap target (not
         * just a small corner icon), label left / chevron right — a
         * settings-list-style row instead of a floating gear button. */
        ListRow warn_row
            = create_list_row(screen_, theme, "Warning Thresholds");
        lv_obj_set_size(warn_row.btn, row_width, ROW_HEIGHT);
        lv_obj_align(warn_row.btn,
                     LV_ALIGN_TOP_MID,
                     0,
                     portrait ? PORTRAIT_WARN_ROW_Y : WARN_ROW_Y);
        lv_obj_add_event_cb(
            warn_row.btn, settings_cb_handler, LV_EVENT_CLICKED, this);
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

        /**
         * Re-align every time, not just once at creation — the label's
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

        /**
         * has_button_ctrl scans for the actually-checked button rather than
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
