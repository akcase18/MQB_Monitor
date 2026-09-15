/**
 * @file simulator_touch.cpp
 * @brief ITouch over SDL2 mouse events.
 *
 * The SDL event pump used to live here, on the reasoning that read() runs
 * every main-loop iteration. It moved to simulator_events.cpp once buttons
 * needed the same events -- and because a touchless product supplies a
 * no-op ITouch that is never polled, which would have left the window
 * unable to process even its own close box.
 */

#include "simulator_touch.hpp"

#include "simulator_display.hpp"
#include "simulator_events.hpp"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

SimulatorTouch &SimulatorTouch::instance()
{
    static SimulatorTouch s_instance;
    return s_instance;
}

void SimulatorTouch::begin()
{
    SDL_InitSubSystem(SDL_INIT_VIDEO);
}

void SimulatorTouch::set_orientation(DisplayOrientation orientation)
{
    /**
     * No-op -- SDL mouse events already report coordinates in the live
     * window's pixel space, and SimulatorDisplay::set_orientation()
     * resizes that window before it's ever created (see
     * simulator_display.cpp), so there's nothing left to remap here. */
    (void)orientation;
}

bool SimulatorTouch::read(TouchPoint &out)
{
    simulator_events::pump();

    uint16_t panel_w = 0;
    uint16_t panel_h = 0;
    SimulatorDisplay::instance().get_resolution(panel_w, panel_h);

    int16_t x = simulator_events::mouse_x();
    int16_t y = simulator_events::mouse_y();

    /**
     * Clicks in the button strip are not screen touches. Without this
     * gate, pressing a simulated button would also land as a tap on
     * whatever LVGL object sits nearest the right edge -- the window is
     * wider than the panel now, but LVGL only knows about the panel. */
    bool pressed = simulator_events::mouse_pressed()
                   && x >= 0 && x < static_cast<int16_t>(panel_w)
                   && y >= 0 && y < static_cast<int16_t>(panel_h);

    out.x       = x;
    out.y       = y;
    out.pressed = pressed;
    return pressed;
}
