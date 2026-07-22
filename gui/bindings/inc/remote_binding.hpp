/**
 * @file remote_binding.hpp
 * @brief The bridge between remote::server::RemoteApController and
 * gui::WirelessSetupScreen — same role VehicleBinding has for core::/
 * gui::widgets, just for the AP lifecycle instead of vehicle data.
 * WirelessSetupScreen itself never includes remote/, matching every other
 * gui/screens screen's "driven by fabricated view-models" discipline.
 */
#pragma once

#include "remote_ap_controller.hpp"
#include "wireless_setup_screen.hpp"

namespace gui
{

    class RemoteBinding
    {
        public:
            explicit RemoteBinding(remote::server::RemoteApController &ap);

            /** No-op unless the AP is currently Idle — see
             * RemoteApController::activate(). */
            void activate();

            /** No-op unless the AP is currently Active — see
             * RemoteApController::deactivate(). */
            void deactivate();

            WirelessSetupViewModel build_view_model() const;

        private:
            remote::server::RemoteApController &ap_;
    };

} // namespace gui
