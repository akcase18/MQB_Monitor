/**
 * @file simulator_touch.hpp
 * @brief Simulator ITouch implementation. Phase 1 stub — SDL2 mouse
 * polling wiring lands in Phase 2.
 */
#pragma once

#include "platform.hpp"

class SimulatorTouch : public ITouch
{
    public:
        static SimulatorTouch &instance();

        void begin() override;
        bool read(TouchPoint &out) override;

    private:
        SimulatorTouch() = default;
};
