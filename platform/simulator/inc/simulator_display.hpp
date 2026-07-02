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
        void get_resolution(uint16_t &width, uint16_t &height) const override;
        void flush(int32_t         x1,
                   int32_t         y1,
                   int32_t         x2,
                   int32_t         y2,
                   const uint16_t *pixels) override;
        bool flush_ready() const override;
        void set_backlight(uint8_t pct) override;

    private:
        SimulatorDisplay() = default;

        /* Opaque to keep SDL2 out of this header, matching how platform/
         * vendor headers stay confined to the .cpp files. */
        void *window_   = nullptr;
        void *renderer_ = nullptr;
        void *texture_  = nullptr;
};
