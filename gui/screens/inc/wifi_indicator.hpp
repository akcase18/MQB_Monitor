/**
 * @file wifi_indicator.hpp
 * @brief The Wi-Fi glyph in the bottom-left corner while the AP is up,
 * over every screen.
 *
 * The indicator is grey until a device connects, then turns the accent color.
 * It lives on lv_layer_top() rather than on any one screen, so it stays put
 * across screen loads.
 *
 * This is used to tell the user when WiFi is active, as well as when a device
 * is connected.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{

    class WifiIndicator
    {
        public:
            /**
             * Creates the glyph, hidden.
             */
            void init(const Theme &theme);
            void destroy();

            /**
             * `joined` only matters while `active`.
             */
            void set_state(bool active, bool joined);

        private:
            lv_obj_t  *icon_ = nullptr;
            lv_color_t idle_ {};
            lv_color_t joined_ {};
            bool       shows_joined_ = false;
    };

} // namespace gui
