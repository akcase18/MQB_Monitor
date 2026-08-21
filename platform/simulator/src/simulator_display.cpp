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

#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace
{
    constexpr uint16_t SIM_HOR_RES = 480;
    constexpr uint16_t SIM_VER_RES = 320;

    const char *position_file_path()
    {
        const char *home = getenv("HOME");
        static char path[512];
        snprintf(path,
                 sizeof(path),
                 "%s/.mqb_sim_window_pos",
                 home != nullptr ? home : "/tmp");
        return path;
    }

    bool load_saved_position(int &x, int &y)
    {
        FILE *f = fopen(position_file_path(), "r");
        if (f == nullptr)
        {
            return false;
        }
        bool ok = fscanf(f, "%d %d", &x, &y) == 2;
        fclose(f);
        return ok;
    }

    void atexit_save_position()
    {
        SimulatorDisplay::instance().save_position();
    }

    /* exit() (rather than the signal's default terminate-immediately
     * behavior) so the atexit_save_position handler above still runs. */
    void handle_terminating_signal(int)
    {
        exit(0);
    }
} // namespace

SimulatorDisplay &SimulatorDisplay::instance()
{
    static SimulatorDisplay s_instance;
    return s_instance;
}

void SimulatorDisplay::begin()
{
    SDL_InitSubSystem(SDL_INIT_VIDEO);

    int win_x = SDL_WINDOWPOS_CENTERED;
    int win_y = SDL_WINDOWPOS_CENTERED;
    load_saved_position(win_x, win_y);

    SDL_Window *window = SDL_CreateWindow("MQB Monitor Simulator",
                                          win_x,
                                          win_y,
                                          width_,
                                          height_,
                                          SDL_WINDOW_SHOWN);

    SDL_Renderer *renderer
        = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    SDL_Texture *texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_RGB565,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             width_,
                                             height_);

    window_   = window;
    renderer_ = renderer;
    texture_  = texture;

    atexit(atexit_save_position);
    signal(SIGTERM, handle_terminating_signal);
    signal(SIGINT, handle_terminating_signal);
}

void SimulatorDisplay::set_orientation(DisplayOrientation orientation)
{
    if (orientation == DisplayOrientation::Portrait)
    {
        width_  = SIM_VER_RES;
        height_ = SIM_HOR_RES;
    }
    else
    {
        width_  = SIM_HOR_RES;
        height_ = SIM_VER_RES;
    }

    /* src/main.cpp calls begin() before this (TeensyDisplay needs its
     * SPI/panel init done first, so that's the shared call order) -- the
     * window/texture begin() already created at the old size need to be
     * resized/recreated here to match, rather than just updating
     * width_/height_ for a future begin() to read. */
    SDL_Window *window = static_cast<SDL_Window *>(window_);
    if (window == nullptr)
    {
        return;
    }

    SDL_SetWindowSize(window, width_, height_);

    SDL_DestroyTexture(static_cast<SDL_Texture *>(texture_));
    texture_ = SDL_CreateTexture(static_cast<SDL_Renderer *>(renderer_),
                                 SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 width_,
                                 height_);
}

void SimulatorDisplay::save_position() const
{
    SDL_Window *window = static_cast<SDL_Window *>(window_);
    if (window == nullptr)
    {
        return;
    }

    int x = 0;
    int y = 0;
    SDL_GetWindowPosition(window, &x, &y);

    FILE *f = fopen(position_file_path(), "w");
    if (f != nullptr)
    {
        fprintf(f, "%d %d", x, y);
        fclose(f);
    }
}

void SimulatorDisplay::get_resolution(uint16_t &width, uint16_t &height) const
{
    width  = width_;
    height = height_;
}

void SimulatorDisplay::flush(
    int32_t x1, int32_t y1, int32_t x2, int32_t y2, const uint16_t *pixels)
{
    SDL_Rect rect { x1, y1, x2 - x1 + 1, y2 - y1 + 1 };
    SDL_UpdateTexture(static_cast<SDL_Texture *>(texture_),
                      &rect,
                      pixels,
                      rect.w * static_cast<int>(sizeof(uint16_t)));

    SDL_Renderer *renderer = static_cast<SDL_Renderer *>(renderer_);
    SDL_RenderCopy(
        renderer, static_cast<SDL_Texture *>(texture_), nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

void SimulatorDisplay::set_backlight(uint8_t pct)
{
    /* No real backlight to dim on a dev host. */
    (void)pct;
}
