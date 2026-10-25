/**
 * @file simulator_touch.hpp
 * @brief Simulator ITouch implementation, backed by SDL2 mouse polling.
 */
#pragma once

#include "platform.hpp"

class SimulatorTouch : public ITouch
{
    public:
        static SimulatorTouch &instance();

        void begin() override;
        void set_orientation(DisplayOrientation orientation,
                             bool               flipped) override;
        bool read(TouchPoint &out) override;

    private:
        SimulatorTouch() = default;

        DisplayOrientation orientation_ = DisplayOrientation::Landscape;
        bool               flipped_     = false;
};
