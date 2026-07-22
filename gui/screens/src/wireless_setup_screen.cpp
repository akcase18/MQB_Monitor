#include "wireless_setup_screen.hpp"

#include <stdio.h>
#include <string.h>

#include "screen_helpers.hpp"

namespace gui
{

    namespace
    {
        constexpr lv_coord_t HEADER_DIVIDER_Y = 50;

        constexpr lv_coord_t SPINNER_SIZE       = 56;
        constexpr lv_coord_t SPINNER_Y          = 120;
        constexpr lv_coord_t CONNECTING_LABEL_Y = 192;

        constexpr lv_coord_t QR_SIZE = 130;
        /* Vertically centers the QR+SSID+URL block (~186px tall) within
         * active_group_'s 270px height (screen_height=320 minus
         * HEADER_DIVIDER_Y=50) — a symmetric ~42px margin above the QR
         * and below the URL line, rather than the block sitting flush
         * against the header divider. */
        constexpr lv_coord_t QR_Y          = 92;
        constexpr lv_coord_t ACTIVE_SSID_Y = 234;
        constexpr lv_coord_t ACTIVE_URL_Y  = 256;
    } // namespace

    void WirelessSetupScreen::init(const Theme       &theme,
                                   DeactivateCallback deactivate_cb,
                                   void              *deactivate_ctx)
    {
        deactivate_cb_  = deactivate_cb;
        deactivate_ctx_ = deactivate_ctx;

        screen_ = create_screen(theme);

        lv_obj_t *title_lbl = lv_label_create(screen_);
        lv_label_set_text(title_lbl, "Wireless Setup");
        lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 20, 12);
        lv_obj_set_style_text_color(title_lbl, theme.palette.fg, 0);
        lv_obj_set_style_text_font(title_lbl, theme.typography.title, 0);

        DoneButton close = create_close_button(screen_, theme);
        lv_obj_add_event_cb(
            close.btn, close_clicked_cb, LV_EVENT_CLICKED, this);

        create_divider(screen_, theme, HEADER_DIVIDER_Y);

        /* Connecting group: spinner + status label — covers Idle (opening
         * this screen activates the AP immediately, see the file doc
         * comment, so Idle is only ever momentarily true if at all),
         * Activating, and Deactivating (see RemoteBinding::build_view_
         * model()). No Activate button — there's nothing left for the
         * user to tap here. */
        connecting_group_ = lv_obj_create(screen_);
        lv_obj_remove_style_all(connecting_group_);
        lv_obj_set_size(connecting_group_,
                        theme.metrics.screen_width,
                        theme.metrics.screen_height - HEADER_DIVIDER_Y);
        lv_obj_set_pos(connecting_group_, 0, HEADER_DIVIDER_Y);

        lv_obj_t *spinner = lv_spinner_create(connecting_group_);
        lv_obj_set_size(spinner, SPINNER_SIZE, SPINNER_SIZE);
        lv_obj_align(
            spinner, LV_ALIGN_TOP_MID, 0, SPINNER_Y - HEADER_DIVIDER_Y);
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
                     CONNECTING_LABEL_Y - HEADER_DIVIDER_Y);

        /* Active group: QR code + credentials fallback. Deactivating is
         * the close (X) button's job (see the file doc comment) — no
         * separate control needed here. */
        active_group_ = lv_obj_create(screen_);
        lv_obj_remove_style_all(active_group_);
        lv_obj_set_size(active_group_,
                        theme.metrics.screen_width,
                        theme.metrics.screen_height - HEADER_DIVIDER_Y);
        lv_obj_set_pos(active_group_, 0, HEADER_DIVIDER_Y);

        qrcode_ = lv_qrcode_create(active_group_);
        lv_qrcode_set_size(qrcode_, QR_SIZE);
        lv_qrcode_set_dark_color(qrcode_, theme.palette.fg);
        lv_qrcode_set_light_color(qrcode_, theme.palette.bg);
        lv_obj_align(qrcode_, LV_ALIGN_TOP_MID, 0, QR_Y - HEADER_DIVIDER_Y);

        ssid_lbl_ = lv_label_create(active_group_);
        lv_obj_set_style_text_color(ssid_lbl_, theme.palette.fg, 0);
        lv_obj_set_style_text_font(ssid_lbl_, theme.typography.label_lg, 0);
        lv_obj_align(
            ssid_lbl_, LV_ALIGN_TOP_MID, 0, ACTIVE_SSID_Y - HEADER_DIVIDER_Y);

        url_lbl_ = lv_label_create(active_group_);
        lv_obj_set_style_text_color(
            url_lbl_, lv_color_darken(theme.palette.fg, 60), 0);
        lv_obj_set_style_text_font(url_lbl_, theme.typography.label_lg, 0);
        lv_obj_align(
            url_lbl_, LV_ALIGN_TOP_MID, 0, ACTIVE_URL_Y - HEADER_DIVIDER_Y);
    }

    void WirelessSetupScreen::open(const WirelessSetupViewModel &vm,
                                   lv_obj_t                     *return_screen)
    {
        return_screen_ = return_screen;
        /* Force set_view_model() to touch the QR widget even if the phase
         * happens to match last_phase_ from a previous session (e.g.
         * reopening while still Active from before). */
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
            /* Idle and Activating share one visual (see the file doc
             * comment) — only Active gets its own. Un-hide before touching
             * the QR canvas below: lv_qrcode_update()'s internal
             * lv_obj_invalidate() is a no-op while the parent is still
             * hidden. */
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

        /* Optimistic: don't wait around to watch ApState reach Idle (see
         * the file doc comment) — back out to Settings immediately. */
        if (self->return_screen_ != nullptr)
        {
            lv_screen_load(self->return_screen_);
        }
    }

} // namespace gui
