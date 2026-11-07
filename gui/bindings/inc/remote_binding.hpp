/**
 * @file remote_binding.hpp
 * @brief Bridge between remote::server's controllers and the related screens.
 *
 * RemoteApController --> gui::WirelessSetupScreen
 * RemoteOtaController --> gui::FirmwareUpdateScreen
 */
#pragma once

#include "firmware_update_screen.hpp"
#include "remote_ap_controller.hpp"
#include "remote_ota_controller.hpp"
#include "wireless_setup_screen.hpp"

namespace gui
{

    class RemoteBinding
    {
        public:
            RemoteBinding(remote::server::RemoteApController  &ap,
                          remote::server::RemoteOtaController &ota);

            /**
             * No-op unless the AP is currently Idle — see
             * RemoteApController::activate().
             */
            void activate();

            /**
             * No-op unless the AP is currently Active — see
             * RemoteApController::deactivate().
             */
            void deactivate();

            WirelessSetupViewModel build_view_model() const;

            /**
             * Only meaningful in update mode
             * (RemoteOtaController::in_update_mode()).
             */
            FirmwareUpdateViewModel build_update_view_model() const;

        private:
            remote::server::RemoteApController  &ap_;
            remote::server::RemoteOtaController &ota_;
    };

} // namespace gui
