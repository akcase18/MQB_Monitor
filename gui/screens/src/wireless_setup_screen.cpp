#include "wireless_setup_screen.hpp"
#include "mem_placement.hpp"

#include <stdio.h>
#include <string.h>

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        constexpr lv_coord_t HEADER_DIVIDER_Y = 50;

        constexpr lv_coord_t SPINNER_SIZE      = 56;
        constexpr lv_coord_t SPINNER_LABEL_GAP = 16;
        /**
         * Spinner(56) + gap(16) + status label, measured off Landscape's
         * original SPINNER_Y=120 (each exactly reproduced by the formula
         * below at screen_height=320).
         */
        constexpr lv_coord_t CONNECTING_BLOCK_HEIGHT = 130;

        constexpr lv_coord_t QR_SIZE = 130;
        /**
         * QR(130) + gap(12) + SSID line + gap(22) + URL line, measured off
         * Landscape's original QR_Y=92/ACTIVE_SSID_Y=234/ACTIVE_URL_Y=256
         * (each exactly reproduced by the formula below at
         * screen_height=320). Used to vertically center the whole block
         * within active_group_'s height.
         */
        constexpr lv_coord_t QR_BLOCK_HEIGHT = 186;
    } // namespace

    FLASHMEM void WirelessSetupScreen::init(const Theme       &theme,
                                            DeactivateCallback deactivate_cb,
                                            void              *deactivate_ctx)
    {
        deactivate_cb_  = deactivate_cb;
        deactivate_ctx_ = deactivate_ctx;

        screen_ = create_screen(theme);

        create_nav_title(screen_, theme, "WiFi");

        DoneButton close = create_close_button(screen_, theme);
        lv_obj_add_event_cb(
            close.btn, close_clicked_cb, LV_EVENT_CLICKED, this);

        create_divider(screen_, theme, HEADER_DIVIDER_Y);

        /**
         * Connecting group: spinner + status label.
         *
         * Handles Idle (opening this screen activates the AP immediately, see
         * the file doc comment, so Idle is only ever momentarily true if at
         * all), Activating, and Deactivating (see RemoteBinding::build_view_
         * model()). No Activate button, activates on open.
         */
        connecting_group_ = lv_obj_create(screen_);
        lv_obj_remove_style_all(connecting_group_);
        lv_coord_t connecting_group_height
            = theme.metrics.screen_height - HEADER_DIVIDER_Y;
        lv_obj_set_size(connecting_group_,
                        theme.metrics.screen_width,
                        connecting_group_height);
        lv_obj_set_pos(connecting_group_, 0, HEADER_DIVIDER_Y);

        lv_coord_t connecting_margin
            = (connecting_group_height - CONNECTING_BLOCK_HEIGHT) / 2;

        lv_obj_t *spinner = lv_spinner_create(connecting_group_);
        lv_obj_set_size(spinner, SPINNER_SIZE, SPINNER_SIZE);
        lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, connecting_margin);
        lv_obj_set_style_arc_color(
            spinner, theme.palette.accent, LV_PART_INDICATOR);

        lv_obj_t *connecting_lbl = lv_label_create(connecting_group_);
        lv_label_set_text(connecting_lbl, "Starting hotspot...");
        lv_obj_set_style_text_color(connecting_lbl, theme.palette.fg, 0);
        lv_obj_set_style_text_font(
            connecting_lbl, theme.typography.label_lg, 0);
        lv_obj_align(connecting_lbl,
                     LV_ALIGN_TOP_MID,
                     0,
                     connecting_margin + SPINNER_SIZE + SPINNER_LABEL_GAP);

        /**
         * Active group: QR code + credentials fallback. Deactivating is
         * handled by the close button.
         */
        active_group_ = lv_obj_create(screen_);
        lv_obj_remove_style_all(active_group_);
        lv_obj_set_size(active_group_,
                        theme.metrics.screen_width,
                        theme.metrics.screen_height - HEADER_DIVIDER_Y);
        lv_obj_set_pos(active_group_, 0, HEADER_DIVIDER_Y);

        lv_coord_t group_height
            = theme.metrics.screen_height - HEADER_DIVIDER_Y;
        lv_coord_t qr_margin = (group_height - QR_BLOCK_HEIGHT) / 2;
        lv_coord_t qr_y      = HEADER_DIVIDER_Y + qr_margin;
        lv_coord_t ssid_y    = qr_y + QR_SIZE + 12;
        lv_coord_t url_y     = ssid_y + 22;

        qrcode_ = lv_qrcode_create(active_group_);
        lv_qrcode_set_size(qrcode_, QR_SIZE);
        lv_qrcode_set_dark_color(qrcode_, theme.palette.fg);
        lv_qrcode_set_light_color(qrcode_, theme.palette.bg);
        lv_obj_align(qrcode_, LV_ALIGN_TOP_MID, 0, qr_y - HEADER_DIVIDER_Y);

        ssid_lbl_ = lv_label_create(active_group_);
        lv_obj_set_style_text_color(ssid_lbl_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(ssid_lbl_, theme.typography.label_lg, 0);
        lv_obj_align(ssid_lbl_, LV_ALIGN_TOP_MID, 0, ssid_y - HEADER_DIVIDER_Y);

        url_lbl_ = lv_label_create(active_group_);
        lv_obj_set_style_text_color(
            url_lbl_, lv_color_darken(theme.palette.fg, 60), 0);
        lv_obj_set_style_text_font(url_lbl_, theme.typography.label_lg, 0);
        lv_obj_align(url_lbl_, LV_ALIGN_TOP_MID, 0, url_y - HEADER_DIVIDER_Y);
    }

    void WirelessSetupScreen::open(const WirelessSetupViewModel &vm,
                                   lv_obj_t                     *return_screen)
    {
        return_screen_ = return_screen;
        /**
         * Force set_view_model() to update the QR widget even if the phase
         * hasn't changed.
         */
        last_phase_ = static_cast<WirelessSetupPhase>(0xFF);
        set_view_model(vm);
        lv_screen_load(screen_);
    }

    void WirelessSetupScreen::refresh(const WirelessSetupViewModel &vm)
    {
        set_view_model(vm);
    }

    void WirelessSetupScreen::set_view_model(const WirelessSetupViewModel &vm)
    {
        if (vm.phase != last_phase_)
        {
            bool active = vm.phase == WirelessSetupPhase::Active;
            lv_obj_set_hidden(connecting_group_, active);
            lv_obj_set_hidden(active_group_, !active);
        }

        if (vm.phase == WirelessSetupPhase::Active
            && last_phase_ != WirelessSetupPhase::Active)
        {
            lv_qrcode_update(qrcode_, vm.qr_payload, strlen(vm.qr_payload));

            char ssid_line[48];
            snprintf(ssid_line, sizeof(ssid_line), "Wi-Fi: %s", vm.ssid);
            lv_label_set_text(ssid_lbl_, ssid_line);

            lv_label_set_text(url_lbl_, vm.url_text);
        }

        last_phase_ = vm.phase;
    }

    void WirelessSetupScreen::close_clicked_cb(lv_event_t *e)
    {
        auto *self
            = static_cast<WirelessSetupScreen *>(lv_event_get_user_data(e));
        if (self->deactivate_cb_ != nullptr)
        {
            self->deactivate_cb_(self->deactivate_ctx_);
        }

        /**
         * Optimistic: don't wait around to watch ApState reach Idle before
         * backing out to settings.
         */
        if (self->return_screen_ != nullptr)
        {
            lv_screen_load(self->return_screen_);
        }
    }

} // namespace gui
