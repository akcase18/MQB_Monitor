/**
 * @file esp32s3_buttons.hpp
 * @brief IButtons over two GPIOs.
 *
 * Raw level and nothing else -- debounce, gestures and the chord all live
 * in core/input, where they are pure functions with an injected time and
 * a host test suite. A driver that debounced here would be duplicating
 * tested logic in the one place it cannot be tested.
 *
 * Active-low against the internal pull-ups. External pull-ups and ESD
 * protection are still wanted on the board (the build plan's 6.4); the
 * internal ones mean a bare switch works on a bench jig with nothing else
 * fitted.
 */
#pragma once

#include "platform.hpp"

class Esp32s3Buttons : public IButtons
{
    public:
        static Esp32s3Buttons &instance();

        void    begin() override;
        uint8_t count() const override;
        bool    is_down(uint8_t idx) const override;

    private:
        Esp32s3Buttons() = default;
};
