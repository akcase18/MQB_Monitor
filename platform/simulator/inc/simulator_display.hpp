/**
 * @file simulator_display.hpp
 * @brief Simulator IDisplay implementation over raw SDL2 (not LVGL's own
 * SDL driver — see platform/simulator/src/simulator_display.cpp for why).
 */
#pragma once

#include "platform.hpp"
#include "simulator_config.hpp"

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

        /**
         * Writes the window's current on-screen position to disk so the
         * next launch can reopen there instead of recentering — see
         * begin()'s load side in simulator_display.cpp. Public so the
         * atexit/SIGTERM/SIGINT handlers registered in begin() can call it
         * however the dev workflow kills the simulator binary (window
         * close, pkill, Ctrl+C). */
        void save_position() const;

        /**
         * Repaints the window from the last flushed frame, plus the button
         * chrome on top.
         *
         * Called by SimulatorButtons when a button changes state. Without
         * it a press would not show until LVGL next happened to invalidate
         * something, which for a static screen could be never -- the button
         * would work but look dead.
         */
        void present();

    private:
        SimulatorDisplay() = default;

        /**
         * Paints the button strip beside the emulated panel. Geometry comes
         * from SimulatorButtons so the drawn rect and the hit-tested rect
         * are the same rect. */
        void draw_chrome();

        /**
         * Opaque to keep SDL2 out of this header, matching how platform/
         * vendor headers stay confined to the .cpp files. */
        void *window_   = nullptr;
        void *renderer_ = nullptr;
        void *texture_  = nullptr;

        uint16_t width_  = SIM_PANEL_W;
        uint16_t height_ = SIM_PANEL_H;
};
