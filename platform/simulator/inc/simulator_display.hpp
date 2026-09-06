/**
 * @file simulator_display.hpp
 * @brief Simulator IDisplay implementation over raw SDL2 (not LVGL's own
 * SDL driver — see platform/simulator/src/simulator_display.cpp for why).
 */
#pragma once

#include "platform.hpp"

class SimulatorDisplay : public IDisplay
{
    public:
        static SimulatorDisplay &instance();

        void begin() override;
        void set_orientation(DisplayOrientation orientation) override;
        void get_resolution(uint16_t &width, uint16_t &height) const override;
        void flush(int32_t         x1,
                   int32_t         y1,
                   int32_t         x2,
                   int32_t         y2,
                   const uint16_t *pixels) override;
        void set_backlight(uint8_t pct) override;
        void set_backlight_off() override;

        /* Writes the window's current on-screen position to disk so the
         * next launch can reopen there instead of recentering — see
         * begin()'s load side in simulator_display.cpp. Public so the
         * atexit/SIGTERM/SIGINT handlers registered in begin() can call it
         * however the dev workflow kills the simulator binary (window
         * close, pkill, Ctrl+C). */
        void save_position() const;

    private:
        SimulatorDisplay() = default;

        /* Opaque to keep SDL2 out of this header, matching how platform/
         * vendor headers stay confined to the .cpp files. */
        void *window_   = nullptr;
        void *renderer_ = nullptr;
        void *texture_  = nullptr;

        uint16_t width_  = 480;
        uint16_t height_ = 320;
};
