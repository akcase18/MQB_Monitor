#include "simulator_events.hpp"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdlib.h>

namespace simulator_events
{

    namespace
    {
        constexpr uint8_t BUTTON_KEY_COUNT = 2;

        int16_t s_mouse_x       = 0;
        int16_t s_mouse_y       = 0;
        bool    s_mouse_pressed = false;

        bool s_key_down[BUTTON_KEY_COUNT] = { false, false };
        bool s_fault_key_edge             = false;
        bool s_pressure_drop_key          = false;

        /**
         * Returns the button index a keycode drives, or -1. 'a'/'b' are the
         * primary bindings (matching the action map's Button A / Button B);
         * '1'/'2' are aliases for anyone who reaches for numbers. */
        int key_to_button(SDL_Keycode key)
        {
            switch (key)
            {
                case SDLK_a:
                case SDLK_1:
                    return 0;
                case SDLK_b:
                case SDLK_2:
                    return 1;
                default:
                    return -1;
            }
        }
    } // namespace

    void pump()
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
                        s_mouse_pressed = true;
                        s_mouse_x       = static_cast<int16_t>(event.button.x);
                        s_mouse_y       = static_cast<int16_t>(event.button.y);
                    }
                    break;

                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT)
                    {
                        s_mouse_pressed = false;
                    }
                    break;

                case SDL_MOUSEMOTION:
                    if (s_mouse_pressed)
                    {
                        s_mouse_x = static_cast<int16_t>(event.motion.x);
                        s_mouse_y = static_cast<int16_t>(event.motion.y);
                    }
                    break;

                case SDL_KEYDOWN:
                {
                    /**
                     * Ignore auto-repeat: a held key is already down, and
                     * letting repeats through would look like a stream of
                     * fresh presses to anything edge-sensitive. */
                    if (event.key.repeat != 0)
                    {
                        break;
                    }
                    if (event.key.keysym.sym == SDLK_f)
                    {
                        s_fault_key_edge = true;
                    }
                    if (event.key.keysym.sym == SDLK_p)
                    {
                        s_pressure_drop_key = true;
                    }

                    int idx = key_to_button(event.key.keysym.sym);
                    if (idx >= 0)
                    {
                        s_key_down[idx] = true;
                    }
                    break;
                }

                case SDL_KEYUP:
                {
                    if (event.key.keysym.sym == SDLK_p)
                    {
                        s_pressure_drop_key = false;
                    }

                    int idx = key_to_button(event.key.keysym.sym);
                    if (idx >= 0)
                    {
                        s_key_down[idx] = false;
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }

    bool mouse_pressed()
    {
        return s_mouse_pressed;
    }

    int16_t mouse_x()
    {
        return s_mouse_x;
    }

    int16_t mouse_y()
    {
        return s_mouse_y;
    }

    bool consume_fault_key()
    {
        bool edge        = s_fault_key_edge;
        s_fault_key_edge = false;
        return edge;
    }

    bool pressure_drop_key_down()
    {
        return s_pressure_drop_key;
    }

    bool button_key_down(uint8_t idx)
    {
        if (idx >= BUTTON_KEY_COUNT)
        {
            return false;
        }
        return s_key_down[idx];
    }

} // namespace simulator_events
