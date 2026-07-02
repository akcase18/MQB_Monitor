/**
 * @file simulator_display.cpp
 * @brief Raw SDL2 window/renderer/texture backend for IDisplay. Deliberately
 * does not use LVGL's own SDL driver (lv_sdl_window_create/lv_sdl_mouse_create)
 * — those register the lv_display_t/lv_indev_t themselves, which would
 * conflict with gui/ owning display/indev registration uniformly across
 * every platform via this interface.
 */

#include "simulator_display.hpp"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

namespace
{
constexpr uint16_t SIM_HOR_RES = 480;
constexpr uint16_t SIM_VER_RES = 320;
} // namespace

SimulatorDisplay &SimulatorDisplay::instance()
{
    static SimulatorDisplay s_instance;
    return s_instance;
}

void SimulatorDisplay::begin()
{
    SDL_InitSubSystem(SDL_INIT_VIDEO);

    SDL_Window *window = SDL_CreateWindow("MQB Monitor Simulator",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          SIM_HOR_RES,
                                          SIM_VER_RES,
                                          SDL_WINDOW_SHOWN);

    SDL_Renderer *renderer
        = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    SDL_Texture *texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_RGB565,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             SIM_HOR_RES,
                                             SIM_VER_RES);

    window_   = window;
    renderer_ = renderer;
    texture_  = texture;
}

void SimulatorDisplay::get_resolution(uint16_t &width, uint16_t &height) const
{
    width  = SIM_HOR_RES;
    height = SIM_VER_RES;
}

void SimulatorDisplay::flush(int32_t         x1,
                             int32_t         y1,
                             int32_t         x2,
                             int32_t         y2,
                             const uint16_t *pixels)
{
    SDL_Rect rect{ x1, y1, x2 - x1 + 1, y2 - y1 + 1 };
    SDL_UpdateTexture(static_cast<SDL_Texture *>(texture_),
                      &rect,
                      pixels,
                      rect.w * static_cast<int>(sizeof(uint16_t)));

    SDL_Renderer *renderer = static_cast<SDL_Renderer *>(renderer_);
    SDL_RenderCopy(renderer, static_cast<SDL_Texture *>(texture_), nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

bool SimulatorDisplay::flush_ready() const
{
    return true;
}

void SimulatorDisplay::set_backlight(uint8_t pct)
{
    /* No real backlight to dim on a dev host. */
    (void)pct;
}
