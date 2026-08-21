#include "theme_picker_screen.hpp"

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        constexpr lv_coord_t HEADER_DIVIDER_Y = 50;
        /* 3x3 for ACCENT_PRESET_COUNT=9 — smaller than the original 4x2's
         * swatches since a 3rd row has to fit in the same screen height. */
        constexpr lv_coord_t SWATCH_SIZE = 76;
        constexpr lv_coord_t GRID_GAP    = 12;
        constexpr uint8_t    GRID_COLS   = 3;
        constexpr uint8_t    GRID_ROWS   = 3;
        /* Thicker than the unselected outline so the current pick reads
         * clearly at a glance against 8 otherwise-identical swatches. */
        constexpr lv_coord_t SELECTED_BORDER_WIDTH = 4;
    } // namespace

    void ThemePickerScreen::init(const Theme &theme,
                                 DoneCallback done_cb,
                                 void        *ctx)
    {
        theme_    = &theme;
        done_cb_  = done_cb;
        done_ctx_ = ctx;

        screen_ = create_screen(theme);

        lv_obj_t *title_lbl = lv_label_create(screen_);
        lv_label_set_text(title_lbl, "Accent Color");
        lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 20, 12);
        lv_obj_set_style_text_color(title_lbl, theme.palette.fg, 0);
        lv_obj_set_style_text_font(title_lbl, theme.typography.title, 0);

        DoneButton close = create_close_button(screen_, theme);
        lv_obj_add_event_cb(
            close.btn, close_cb_handler, LV_EVENT_CLICKED, this);

        create_divider(screen_, theme, HEADER_DIVIDER_Y);

        lv_coord_t grid_width
            = GRID_COLS * SWATCH_SIZE + (GRID_COLS - 1) * GRID_GAP;
        lv_coord_t start_x = (theme.metrics.screen_width - grid_width) / 2;

        /* Vertically center the grid within whatever height is actually
         * available below the header divider, rather than a fixed
         * GRID_START_Y tuned only for Landscape's 270px — Portrait's 430px
         * left the grid stranded near the top with a large dead zone
         * below it. */
        lv_coord_t grid_height
            = GRID_ROWS * SWATCH_SIZE + (GRID_ROWS - 1) * GRID_GAP;
        lv_coord_t available_height
            = theme.metrics.screen_height - HEADER_DIVIDER_Y;
        lv_coord_t grid_start_y
            = HEADER_DIVIDER_Y + (available_height - grid_height) / 2;

        for (uint8_t i = 0; i < ACCENT_PRESET_COUNT; i++)
        {
            uint8_t col = i % GRID_COLS;
            uint8_t row = i / GRID_COLS;

            lv_obj_t *swatch = lv_btn_create(screen_);
            swatches_[i]     = swatch;
            lv_obj_set_size(swatch, SWATCH_SIZE, SWATCH_SIZE);
            lv_obj_set_pos(swatch,
                           start_x + col * (SWATCH_SIZE + GRID_GAP),
                           grid_start_y + row * (SWATCH_SIZE + GRID_GAP));
            lv_obj_set_style_bg_color(
                swatch, lv_color_hex(ACCENT_PRESETS[i]), 0);
            lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(swatch, 12, 0);
            lv_obj_set_style_shadow_width(swatch, 0, 0);
            lv_obj_set_style_border_color(
                swatch, theme.palette.panel_border, 0);
            lv_obj_set_style_border_width(
                swatch, theme.metrics.panel_border_width, 0);
            lv_obj_add_event_cb(
                swatch, swatch_clicked_cb, LV_EVENT_CLICKED, this);
        }
    }

    void ThemePickerScreen::open(uint32_t  current_accent_color,
                                 lv_obj_t *return_screen)
    {
        return_screen_ = return_screen;

        for (uint8_t i = 0; i < ACCENT_PRESET_COUNT; i++)
        {
            bool selected = ACCENT_PRESETS[i] == current_accent_color;
            lv_obj_set_style_border_color(
                swatches_[i],
                selected ? theme_->palette.fg : theme_->palette.panel_border,
                0);
            lv_obj_set_style_border_width(
                swatches_[i],
                selected ? SELECTED_BORDER_WIDTH
                         : theme_->metrics.panel_border_width,
                0);
        }

        lv_screen_load(screen_);
    }

    void ThemePickerScreen::swatch_clicked_cb(lv_event_t *e)
    {
        auto *self
            = static_cast<ThemePickerScreen *>(lv_event_get_user_data(e));
        lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(e));

        /* Unlike SignalPickerScreen, navigation after a pick is done_cb_'s
         * job (matching ConfiguratorScreen's pattern), not this screen's —
         * applying an accent color means rebuilding every other screen
         * against the new color (see main.cpp), which can't safely happen
         * synchronously from inside this very button's own click handler. */
        for (uint8_t i = 0; i < ACCENT_PRESET_COUNT; i++)
        {
            if (self->swatches_[i] == target)
            {
                if (self->done_cb_ != nullptr)
                {
                    self->done_cb_(self->done_ctx_, ACCENT_PRESETS[i]);
                }
                break;
            }
        }
    }

    void ThemePickerScreen::close_cb_handler(lv_event_t *e)
    {
        auto *self
            = static_cast<ThemePickerScreen *>(lv_event_get_user_data(e));
        if (self->return_screen_ != nullptr)
        {
            lv_screen_load(self->return_screen_);
        }
    }

} // namespace gui
