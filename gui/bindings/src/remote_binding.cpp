#include "remote_binding.hpp"
#include "mem_placement.hpp"

#include <stdio.h>

namespace gui
{
    using remote::server::ApState;
    using remote::server::OtaState;

    RemoteBinding::RemoteBinding(remote::server::RemoteApController  &ap,
                                 remote::server::RemoteOtaController &ota)
        : ap_(ap)
        , ota_(ota)
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
                snprintf(
                    vm.qr_payload, sizeof(vm.qr_payload), "%s", vm.url_text);
                break;

            case ApState::Activating:
            case ApState::Deactivating:
                /**
                 * Deactivating is the same state as Activating since it's just
                 * a transition state.
                 */
                vm.phase = WirelessSetupPhase::Activating;
                break;

            case ApState::Idle:
            default:
                vm.phase = WirelessSetupPhase::Idle;
                break;
        }

        return vm;
    }

    FLASHMEM FirmwareUpdateViewModel
    RemoteBinding::build_update_view_model() const
    {
        FirmwareUpdateViewModel vm;
        vm.percent = ota_.percent();
        snprintf(vm.version, sizeof(vm.version), "%s", ota_.version());

        switch (ota_.state())
        {
            case OtaState::Receiving:
                vm.phase = FirmwareUpdatePhase::Receiving;
                break;
            case OtaState::Ready:
                vm.phase = FirmwareUpdatePhase::Ready;
                break;
            case OtaState::Installing:
                vm.phase = FirmwareUpdatePhase::Installing;
                break;
            case OtaState::Armed:
            case OtaState::Idle:
            case OtaState::Requested:
            default:
                vm.phase = FirmwareUpdatePhase::Waiting;
                vm.upload_failed
                    = ota_.last_result() != remote::protocol::OtaResult::Ok;
                break;
        }
        return vm;
    }

} // namespace gui
