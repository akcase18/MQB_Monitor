/**
 * @file simulator_touch.cpp
 * @brief ITouch over SDL2 mouse events. Also the place that pumps the SDL
 * event queue, since read() is called once per main-loop iteration.
 */

#include "simulator_touch.hpp"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdlib.h>

namespace
{
int16_t s_x       = 0;
int16_t s_y       = 0;
bool    s_pressed = false;
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
    return s_pressed;
}
