#include "remote_binding.hpp"

#include <stdio.h>

namespace gui
{
    using remote::server::ApState;

    RemoteBinding::RemoteBinding(remote::server::RemoteApController &ap)
        : ap_(ap)
    {
    }

    void RemoteBinding::activate()
    {
        ap_.activate();
    }

    void RemoteBinding::deactivate()
    {
        ap_.deactivate();
    }

    WirelessSetupViewModel RemoteBinding::build_view_model() const
    {
        WirelessSetupViewModel vm;

        switch (ap_.state())
        {
            case ApState::Active:
                vm.phase = WirelessSetupPhase::Active;
                snprintf(vm.ssid, sizeof(vm.ssid), "%s", ap_.ssid());
                snprintf(vm.url_text,
                         sizeof(vm.url_text),
                         "http://%s",
                         ap_.ip_text());
                /* The QR opens the config site, not a Wi-Fi join — the AP
                 * has no password (see RemoteApController's file doc
                 * comment) so joining is just picking the SSID by hand;
                 * the QR's job is the second step, once already on the
                 * network. Reuse url_text rather than re-deriving it. */
                snprintf(
                    vm.qr_payload, sizeof(vm.qr_payload), "%s", vm.url_text);
                break;

            case ApState::Activating:
            case ApState::Deactivating:
                /* Deactivating folds into Activating's "please wait" view
                 * rather than getting its own phase — it's brief (<=
                 * AP_DISABLE_GRACE_MS) and only ever entered by the user's
                 * own close (X) tap, which WirelessSetupScreen answers by
                 * returning to Settings immediately rather than waiting
                 * around to watch it finish. */
                vm.phase = WirelessSetupPhase::Activating;
                break;

            case ApState::Idle:
            default:
                vm.phase = WirelessSetupPhase::Idle;
                break;
        }

        return vm;
    }

} // namespace gui
