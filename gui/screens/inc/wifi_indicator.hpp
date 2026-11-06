/**
 * @file wifi_indicator.hpp
 * @brief The Wi-Fi glyph in the bottom-left corner while the AP is up,
 * over every screen: grey while nobody is joined, the accent once a
 * phone is. It lives on lv_layer_top() rather than on any one
 * screen, so it stays put across screen loads -- including the Wi-Fi and
 * firmware-update screens, where the phone is connected.
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
             * Creates the glyph, hidden. Not tied to a screen, so a
             * screen rebuild (a new accent) must destroy() and init()
             * it again to recolour it.
             */
            void init(const Theme &theme);
            void destroy();

            /**
             * `phone_joined` only matters while `active`. Cheap to
             * repeat with the same values.
             */
            void set_state(bool active, bool phone_joined);

        private:
            lv_obj_t  *icon_   = nullptr;
            lv_color_t idle_   {};
            lv_color_t joined_ {};
            bool       shows_joined_ = false;
    };

} // namespace gui
