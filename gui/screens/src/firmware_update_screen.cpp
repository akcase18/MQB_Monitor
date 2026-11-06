#include "firmware_update_screen.hpp"
#include "mem_placement.hpp"

#include <stdio.h>

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        constexpr lv_coord_t HEADER_DIVIDER_Y = 50;

        /**
         * Status line, bar, percent and hint, stacked and centred as one
         * block in the space under the nav bar -- the same centring the
         * Wi-Fi screen's spinner block uses, so it sits right in either
         * orientation.
         */
        constexpr lv_coord_t BAR_HEIGHT         = 14;
        constexpr lv_coord_t BAR_SIDE_MARGIN    = 40;
        constexpr lv_coord_t STATUS_SIDE_MARGIN = 16;
        constexpr lv_coord_t STATUS_TO_BAR      = 40;
        constexpr lv_coord_t BAR_TO_PERCENT     = 26;
        constexpr lv_coord_t PERCENT_TO_HINT    = 40;
        /**
         * No bar while waiting, so the hint closes up under the status
         * line rather than leaving the bar's gap between them.
         */
        constexpr lv_coord_t STATUS_TO_HINT_WAITING = 36;
        constexpr lv_coord_t BLOCK_HEIGHT
            = STATUS_TO_BAR + BAR_TO_PERCENT + PERCENT_TO_HINT + 24;

        /**
         * Matches PanelWidget's bar track. A hex value rather than an
         * lv_color_t global, whose dynamic initializer would land in
         * ITCM on the Teensy.
         */
        constexpr uint32_t TRACK_COLOR = 0x404040;
    } // namespace

    FLASHMEM void FirmwareUpdateScreen::create(const Theme    &theme,
                                               InstallCallback install_cb,
                                               void           *install_ctx,
                                               LeaveCallback   leave_cb,
                                               void           *leave_ctx)
    {
        if (screen_ != nullptr)
        {
            return;
        }

        theme_       = &theme;
        install_cb_  = install_cb;
        install_ctx_ = install_ctx;
        leave_cb_    = leave_cb;
        leave_ctx_   = leave_ctx;

        last_phase_   = static_cast<FirmwareUpdatePhase>(0xFF);
        last_percent_ = 0xFF;

        screen_ = create_screen(theme);
        create_nav_title(screen_, theme, "Firmware update");
        DoneButton close = create_close_button(screen_, theme);
        close_btn_       = close.btn;
        lv_obj_add_event_cb(
            close_btn_, leave_clicked_cb, LV_EVENT_CLICKED, this);
        create_divider(screen_, theme, HEADER_DIVIDER_Y);

        lv_coord_t body_height = theme.metrics.screen_height - HEADER_DIVIDER_Y;
        lv_coord_t top = HEADER_DIVIDER_Y + (body_height - BLOCK_HEIGHT) / 2;

        /**
         * One line, cut short with an ellipsis: a full 31-character
         * version runs past Portrait's 320px, and wrapped it would run
         * into the bar. DOTS only truncates at a fixed height.
         */
        status_lbl_ = lv_label_create(screen_);
        lv_obj_set_style_text_color(status_lbl_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(status_lbl_, theme.typography.label_lg, 0);
        lv_obj_set_style_text_align(status_lbl_, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(status_lbl_, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(status_lbl_,
                        theme.metrics.screen_width - 2 * STATUS_SIDE_MARGIN,
                        lv_font_get_line_height(theme.typography.label_lg));
        lv_obj_align(status_lbl_, LV_ALIGN_TOP_MID, 0, top);

        bar_ = lv_bar_create(screen_);
        lv_bar_set_range(bar_, 0, 100);
        lv_obj_set_size(
            bar_, theme.metrics.screen_width - 2 * BAR_SIDE_MARGIN, BAR_HEIGHT);
        lv_obj_align(bar_, LV_ALIGN_TOP_MID, 0, top + STATUS_TO_BAR);
        lv_obj_set_style_bg_color(
            bar_, lv_color_hex(TRACK_COLOR), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(
            bar_, theme.palette.accent, LV_PART_INDICATOR);

        percent_lbl_ = lv_label_create(screen_);
        lv_obj_set_style_text_color(percent_lbl_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(percent_lbl_, theme.typography.ui, 0);
        lv_obj_align(percent_lbl_,
                     LV_ALIGN_TOP_MID,
                     0,
                     top + STATUS_TO_BAR + BAR_TO_PERCENT);

        /**
         * Placed per phase -- see set_view_model().
         */
        hint_lbl_ = lv_label_create(screen_);
        lv_obj_set_style_text_color(hint_lbl_, theme.palette.panel_border, 0);
        lv_obj_set_style_text_font(hint_lbl_, theme.typography.label_lg, 0);
        hint_y_progress_
            = top + STATUS_TO_BAR + BAR_TO_PERCENT + PERCENT_TO_HINT;
        hint_y_waiting_ = top + STATUS_TO_HINT_WAITING;
    }

    FLASHMEM void FirmwareUpdateScreen::destroy()
    {
        if (screen_ == nullptr)
        {
            return;
        }

        close_popup();
        lv_obj_delete_async(screen_);
        screen_      = nullptr;
        close_btn_   = nullptr;
        status_lbl_  = nullptr;
        bar_         = nullptr;
        percent_lbl_ = nullptr;
        hint_lbl_    = nullptr;
    }

    FLASHMEM void FirmwareUpdateScreen::open(const FirmwareUpdateViewModel &vm)
    {
        /**
         * Force a full repaint of the labels, whatever the last update
         * left behind.
         */
        last_phase_   = static_cast<FirmwareUpdatePhase>(0xFF);
        last_percent_ = 0xFF;
        set_view_model(vm);
        lv_screen_load(screen_);
    }

    FLASHMEM void FirmwareUpdateScreen::refresh(
        const FirmwareUpdateViewModel &vm)
    {
        set_view_model(vm);
    }

    FLASHMEM void FirmwareUpdateScreen::set_view_model(
        const FirmwareUpdateViewModel &vm)
    {
        if (vm.phase != last_phase_)
        {
            char status[64];
            switch (vm.phase)
            {
                case FirmwareUpdatePhase::Waiting:
                    snprintf(status, sizeof(status), "Waiting for firmware");
                    break;
                case FirmwareUpdatePhase::Receiving:
                    snprintf(
                        status, sizeof(status), "Receiving %s", vm.version);
                    break;
                case FirmwareUpdatePhase::Ready:
                    snprintf(status, sizeof(status), "%s is ready", vm.version);
                    break;
                case FirmwareUpdatePhase::Installing:
                default:
                    snprintf(status, sizeof(status), "Restarting...");
                    break;
            }
            lv_label_set_text(status_lbl_, status);

            bool waiting = vm.phase == FirmwareUpdatePhase::Waiting;
            bool no_bar
                = waiting || vm.phase == FirmwareUpdatePhase::Installing;
            lv_obj_set_hidden(bar_, no_bar);
            lv_obj_set_hidden(percent_lbl_, no_bar);
            lv_obj_set_hidden(close_btn_, !waiting);
            lv_obj_align(hint_lbl_,
                         LV_ALIGN_TOP_MID,
                         0,
                         waiting ? hint_y_waiting_ : hint_y_progress_);

            if (vm.phase == FirmwareUpdatePhase::Ready && popup_ == nullptr)
            {
                ConfirmPopup popup = create_confirm_popup(
                    *theme_, "Install and restart?", vm.version, "Install");
                lv_obj_add_event_cb(popup.confirm_btn,
                                    install_clicked_cb,
                                    LV_EVENT_CLICKED,
                                    this);
                lv_obj_add_event_cb(
                    popup.cancel_btn, leave_clicked_cb, LV_EVENT_CLICKED, this);
                popup_ = popup.mbox;
            }
            else if (vm.phase != FirmwareUpdatePhase::Ready)
            {
                close_popup();
            }
        }

        if (vm.phase != last_phase_ || vm.upload_failed != last_failed_)
        {
            const char *hint = "Keep the dash powered on.";
            if (vm.phase == FirmwareUpdatePhase::Waiting)
            {
                /**
                 * Short enough for Portrait's 320px.
                 */
                hint = vm.upload_failed ? "Failed. Try upload again."
                                        : "Upload new firmware file.";
            }
            lv_label_set_text(hint_lbl_, hint);
        }

        if (vm.percent != last_percent_)
        {
            lv_bar_set_value(bar_, vm.percent, LV_ANIM_OFF);
            char percent[8];
            snprintf(percent, sizeof(percent), "%u%%", vm.percent);
            lv_label_set_text(percent_lbl_, percent);
        }

        last_phase_   = vm.phase;
        last_percent_ = vm.percent;
        last_failed_  = vm.upload_failed;
    }

    FLASHMEM void FirmwareUpdateScreen::close_popup()
    {
        if (popup_ != nullptr)
        {
            lv_msgbox_close_async(popup_);
            popup_ = nullptr;
        }
    }

    FLASHMEM void FirmwareUpdateScreen::install_clicked_cb(lv_event_t *e)
    {
        auto *self
            = static_cast<FirmwareUpdateScreen *>(lv_event_get_user_data(e));
        self->close_popup();
        if (self->install_cb_ != nullptr)
        {
            self->install_cb_(self->install_ctx_);
        }
    }

    /**
     * The X and the popup's Cancel alike.
     */
    FLASHMEM void FirmwareUpdateScreen::leave_clicked_cb(lv_event_t *e)
    {
        auto *self
            = static_cast<FirmwareUpdateScreen *>(lv_event_get_user_data(e));
        self->close_popup();
        if (self->leave_cb_ != nullptr)
        {
            self->leave_cb_(self->leave_ctx_);
        }
    }

} // namespace gui
