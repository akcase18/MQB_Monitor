#include "simulator_buttons.hpp"

#include "simulator_display.hpp"
#include "simulator_events.hpp"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

namespace
{
    /**
     * Nominal button size, shrunk if the panel is too short to seat two of
     * them with breathing room -- the gauge product's 172px-tall strip is
     * the case that forces this. */
    constexpr int BTN_MAX_SIZE = 56;
    constexpr int BTN_GAP      = 24;
    constexpr int BTN_MARGIN   = 12;
} // namespace

SimulatorButtons &SimulatorButtons::instance()
{
    static SimulatorButtons s_instance;
    return s_instance;
}

void SimulatorButtons::begin()
{
    SDL_InitSubSystem(SDL_INIT_VIDEO);
}

uint8_t SimulatorButtons::count() const
{
    return BUTTON_COUNT;
}

SimulatorButtons::Rect SimulatorButtons::rect_for(uint8_t idx,
                                                  int     display_w,
                                                  int     display_h) const
{
    /**
     * `if constexpr` rather than a plain `if`, so on a product with no
     * buttons the compiler drops the arithmetic below entirely instead of
     * warning about the constant-zero divide it can see but never
     * reaches. */
    if constexpr (BUTTON_COUNT == 0)
    {
        return Rect {};
    }
    else
    {
        if (idx >= BUTTON_COUNT)
        {
            return Rect {};
        }

        int available = display_h - (2 * BTN_MARGIN) - BTN_GAP;
        int size      = available / BUTTON_COUNT;
        if (size > BTN_MAX_SIZE)
        {
            size = BTN_MAX_SIZE;
        }
        if (size < 1)
        {
            size = 1;
        }

        int stack_h = (size * BUTTON_COUNT) + BTN_GAP;

        Rect r;
        r.w = size;
        r.h = size;
        r.x = display_w + ((CHROME_WIDTH - size) / 2);
        r.y = ((display_h - stack_h) / 2) + (idx * (size + BTN_GAP));
        return r;
    }
}

bool SimulatorButtons::pressed_state(uint8_t idx) const
{
    if (idx >= BUTTON_COUNT)
    {
        return false;
    }

    /**
     * Keyboard first, and independently of the mouse: one cursor cannot
     * press two buttons, so a chord is only reachable via keys (or a key
     * plus a click). */
    if (simulator_events::button_key_down(idx))
    {
        return true;
    }

    if (!simulator_events::mouse_pressed())
    {
        return false;
    }

    uint16_t w = 0;
    uint16_t h = 0;
    SimulatorDisplay::instance().get_resolution(w, h);

    Rect    r = rect_for(idx, w, h);
    int16_t x = simulator_events::mouse_x();
    int16_t y = simulator_events::mouse_y();

    return x >= r.x && x < (r.x + r.w) && y >= r.y && y < (r.y + r.h);
}

bool SimulatorButtons::is_down(uint8_t idx) const
{
    simulator_events::pump();
    repaint_if_changed();
    return pressed_state(idx);
}

void SimulatorButtons::repaint_if_changed() const
{
    /**
     * A press has to show up on screen even when nothing else does. LVGL
     * only flushes when something is invalidated, so on a static screen a
     * held button would produce no visual change at all -- it would work
     * and look broken.
     *
     * Guarded on an actual edge rather than repainting every poll: is_down()
     * runs at the input poll rate, and a full RenderCopy plus present on
     * every one of those would be needless work on an idle screen.
     *
     * Uses pressed_state() rather than is_down(): the latter calls this,
     * and SimulatorDisplay::present() draws the chrome by asking for button
     * state, so anything less careful here recurses until the stack ends. */
    /**
     * +1 so the array is never zero-length on a product with no
     * buttons, where the loop below simply does not run. */
    static bool s_last[BUTTON_COUNT + 1] = {};

    bool changed = false;
    for (uint8_t i = 0; i < BUTTON_COUNT; i++)
    {
        bool now = pressed_state(i);
        if (now != s_last[i])
        {
            s_last[i] = now;
            changed   = true;
        }
    }

    if (changed)
    {
        SimulatorDisplay::instance().present();
    }
}
