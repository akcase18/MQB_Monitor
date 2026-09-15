/**
 * @file null_buttons.hpp
 * @brief IButtons for a board that has none.
 *
 * Platform's members are references, so every board must supply one of
 * everything even when the hardware isn't there. This is the honest answer
 * for buttons: count() is 0, so a caller that iterates buttons does nothing
 * rather than reading a pin that isn't wired to anything.
 *
 * Header-only and board-agnostic -- it lives in platform/ rather than under
 * one board because "this board has no buttons" is not a property of any
 * particular board.
 */
#pragma once

#include "platform.hpp"

class NullButtons : public IButtons
{
    public:
        static NullButtons &instance()
        {
            static NullButtons s_instance;
            return s_instance;
        }

        void begin() override
        {
        }

        uint8_t count() const override
        {
            return 0;
        }

        bool is_down(uint8_t idx) const override
        {
            (void)idx;
            return false;
        }
};
