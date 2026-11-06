/**
 * @file simulator_touch.cpp
 * @brief ITouch over SDL2 mouse events. Also the place that pumps the SDL
 * event queue, since read() is called once per main-loop iteration.
 */

#include "simulator_touch.hpp"

#include "simulator_relay.hpp"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdlib.h>

namespace
{
    int16_t s_x       = 0;
    int16_t s_y       = 0;
    bool    s_pressed = false;

    /**
     * Must track simulator_display.cpp's window size -- only needed to
     * rotate a click 180 degrees within it.
     */
    constexpr uint16_t SIM_HOR_RES = 480;
    constexpr uint16_t SIM_VER_RES = 320;
} // namespace

SimulatorTouch &SimulatorTouch::instance()
{
    static SimulatorTouch s_instance;
    return s_instance;
}

void SimulatorTouch::begin()
{
    SDL_InitSubSystem(SDL_INIT_VIDEO);
}

void SimulatorTouch::set_orientation(DisplayOrientation orientation,
                                     bool               flipped)
{
    /**
     * Landscape/Portrait needs no remap -- SDL mouse events already
     * report coordinates in the live window's pixel space, and
     * SimulatorDisplay::set_orientation() resizes that window to match.
     * A flip does: the window shows the frame rotated 180 degrees, so a
     * click has to be rotated back into it (see read()).
     */
    orientation_ = orientation;
    flipped_     = flipped;
}

bool SimulatorTouch::read(TouchPoint &out)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_QUIT:
                exit(0);
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT)
                {
                    s_pressed = true;
                    s_x       = static_cast<int16_t>(event.button.x);
                    s_y       = static_cast<int16_t>(event.button.y);
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT)
                {
                    s_pressed = false;
                }
                break;

            /**
             * This is the one SDL event pump, so the relay's keys come
             * through here too (see simulator_relay.hpp).
             */
            case SDL_KEYDOWN:
                if (event.key.repeat == 0 && event.key.keysym.sym >= 'a'
                    && event.key.keysym.sym <= 'z')
                {
                    SimulatorRelay::instance().on_key(
                        static_cast<char>(event.key.keysym.sym));
                }
                break;

            case SDL_MOUSEMOTION:
                if (s_pressed)
                {
                    s_x = static_cast<int16_t>(event.motion.x);
                    s_y = static_cast<int16_t>(event.motion.y);
                }
                break;

            default:
                break;
        }
    }

    out.x       = s_x;
    out.y       = s_y;
    out.pressed = s_pressed;
    if (flipped_)
    {
        if (orientation_ == DisplayOrientation::Portrait)
        {
            flip_touch_point(out, SIM_VER_RES, SIM_HOR_RES);
        }
        else
        {
            flip_touch_point(out, SIM_HOR_RES, SIM_VER_RES);
        }
    }
    return s_pressed;
}
