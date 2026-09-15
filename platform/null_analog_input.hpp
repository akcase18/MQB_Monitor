/**
 * @file null_analog_input.hpp
 * @brief IAnalogInput for a board with no analog sensors.
 *
 * Platform's members are references, so every board supplies one of
 * everything even when the hardware isn't there. channel_count() is 0 and
 * every read fails, so a caller that iterates channels does nothing and a
 * caller that reads one is told the value is unknown rather than handed a
 * fabricated zero -- which is the distinction IAnalogInput::read_microvolts()
 * exists to preserve.
 *
 * Header-only and board-agnostic, like null_buttons.hpp: "this board has no
 * ADC" is not a property of any particular board.
 */
#pragma once

#include "platform.hpp"

class NullAnalogInput : public IAnalogInput
{
    public:
        static NullAnalogInput &instance()
        {
            static NullAnalogInput s_instance;
            return s_instance;
        }

        void begin() override
        {
        }

        uint8_t channel_count() const override
        {
            return 0;
        }

        bool read_microvolts(uint8_t ch, uint32_t &out) override
        {
            (void)ch;
            (void)out;
            return false;
        }
};
