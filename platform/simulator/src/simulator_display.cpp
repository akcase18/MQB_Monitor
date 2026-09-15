/**
 * @file simulator_display.cpp
 * @brief Raw SDL2 window/renderer/texture backend for IDisplay. Deliberately
 * does not use LVGL's own SDL driver (lv_sdl_window_create/lv_sdl_mouse_create)
 * — those register the lv_display_t/lv_indev_t themselves, which would
 * conflict with gui/ owning display/indev registration uniformly across
 * every platform via this interface.
 */

#include "simulator_display.hpp"

#include "simulator_buttons.hpp"
#include "simulator_config.hpp"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace
{
    /**
     * The emulated panel, from simulator_config.hpp's build flags -- 480x320
     * for the MQB product, 320x172 for the gauge strip. */
    constexpr uint16_t SIM_HOR_RES = SIM_PANEL_W;
    constexpr uint16_t SIM_VER_RES = SIM_PANEL_H;

    const char *position_file_path()
    {
        const char *home = getenv("HOME");
        static char path[512];
        /**
         * One file per product -- both simulators can be open at once,
         * and they should not overwrite each other's remembered spot. */
        snprintf(path,
                 sizeof(path),
                 "%s/.mqb_sim_window_pos%s",
                 home != nullptr ? home : "/tmp",
                 SIM_HAS_BUTTONS ? "_gauge" : "");
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

    /**
     * exit() (rather than the signal's default terminate-immediately
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

    SDL_Window *window
        = SDL_CreateWindow(SIM_HAS_BUTTONS ? "Gauge Simulator"
                                           : "MQB Monitor Simulator",
                           win_x,
                           win_y,
                           width_ + SimulatorButtons::CHROME_WIDTH,
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

    /**
     * src/main.cpp calls begin() before this (TeensyDisplay needs its
     * SPI/panel init done first, so that's the shared call order) -- the
     * window/texture begin() already created at the old size need to be
     * resized/recreated here to match, rather than just updating
     * width_/height_ for a future begin() to read. */
    SDL_Window *window = static_cast<SDL_Window *>(window_);
    if (window == nullptr)
    {
        return;
    }

    SDL_SetWindowSize(
        window, width_ + SimulatorButtons::CHROME_WIDTH, height_);

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

    present();
}

void SimulatorDisplay::present()
{
    SDL_Renderer *renderer = static_cast<SDL_Renderer *>(renderer_);
    if (renderer == nullptr)
    {
        return;
    }

    /**
     * The panel occupies the left of the window; the chrome strip is to
     * its right. An explicit dest rect is what keeps the texture from
     * being stretched across the chrome -- RenderCopy's nullptr dest means
     * "the whole target", which was correct only while the window was
     * exactly panel-sized. */
    SDL_Rect dest { 0, 0, width_, height_ };
    SDL_RenderCopy(
        renderer, static_cast<SDL_Texture *>(texture_), nullptr, &dest);

    if (SimulatorButtons::CHROME_WIDTH > 0)
    {
        draw_chrome();
    }
    SDL_RenderPresent(renderer);
}

void SimulatorDisplay::set_backlight(uint8_t pct)
{
    /* No real backlight to dim on a dev host. */
    (void)pct;
}

void SimulatorDisplay::set_backlight_off()
{
    /**
     * Nothing to switch off either -- which also means the transitions
     * this hides on real hardware (see IDisplay::set_backlight_off) stay
     * fully visible here. That's the useful behavior for a dev host:
     * blanking the window would hide the very rebuild being worked on. */
}

void SimulatorDisplay::draw_chrome()
{
    SDL_Renderer *renderer = static_cast<SDL_Renderer *>(renderer_);
    if (renderer == nullptr)
    {
        return;
    }

    SimulatorButtons &buttons = SimulatorButtons::instance();

    /* Strip background. */
    SDL_Rect strip { width_,
                     0,
                     SimulatorButtons::CHROME_WIDTH,
                     height_ };
    SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
    SDL_RenderFillRect(renderer, &strip);

    /**
     * A hairline where the chrome meets the panel, so the emulated screen
     * still reads as a distinct object rather than bleeding into the
     * window furniture. */
    SDL_Rect seam { width_, 0, 1, height_ };
    SDL_SetRenderDrawColor(renderer, 70, 74, 80, 255);
    SDL_RenderFillRect(renderer, &seam);

    for (uint8_t i = 0; i < buttons.count(); i++)
    {
        SimulatorButtons::Rect r = buttons.rect_for(i, width_, height_);
        bool down = buttons.pressed_state(i);

        SDL_Rect border { r.x - 2, r.y - 2, r.w + 4, r.h + 4 };
        SDL_SetRenderDrawColor(renderer, 96, 102, 110, 255);
        SDL_RenderFillRect(renderer, &border);

        SDL_Rect face { r.x, r.y, r.w, r.h };
        if (down)
        {
            /**
             * The app's own default accent, so a pressed button reads as
             * "active" in the same visual language as the dashboard. */
            SDL_SetRenderDrawColor(renderer, 76, 175, 80, 255);
        }
        else
        {
            SDL_SetRenderDrawColor(renderer, 52, 56, 62, 255);
        }
        SDL_RenderFillRect(renderer, &face);

        /**
         * Index pips -- one for button A, two for button B. SDL2 has no
         * text without SDL_ttf, and a pip count is unambiguous at this
         * size where a hand-drawn glyph would not be. */
        constexpr int PIP = 7;
        constexpr int PIP_GAP = 6;
        int           pips = static_cast<int>(i) + 1;
        int           span = (pips * PIP) + ((pips - 1) * PIP_GAP);
        int           px   = r.x + ((r.w - span) / 2);
        int           py   = r.y + ((r.h - PIP) / 2);

        SDL_SetRenderDrawColor(renderer, 232, 236, 240, 255);
        for (int p = 0; p < pips; p++)
        {
            SDL_Rect pip { px + (p * (PIP + PIP_GAP)), py, PIP, PIP };
            SDL_RenderFillRect(renderer, &pip);
        }
    }
}
