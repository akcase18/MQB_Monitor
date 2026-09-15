/**
 * @file simulator_buttons.hpp
 * @brief IButtons over SDL2 -- two on-screen buttons in a chrome strip down
 * the right-hand side of the window, pressable by mouse or keyboard.
 *
 * The window is widened by CHROME_WIDTH beyond the panel resolution and the
 * emulated display is drawn into the left part of it, so the buttons sit
 * beside the screen rather than on top of it. Nothing above platform/ ever
 * sees that: IDisplay::get_resolution() still reports the panel size, and
 * clicks in the chrome are not reported as touches.
 *
 * This class owns the button geometry (rect_for()) as well as the input
 * state, and SimulatorDisplay asks it where to draw. That way there is one
 * definition of where a button is, shared by the thing that draws it and
 * the thing that hit-tests it -- they cannot drift apart into a button you
 * can see but not press.
 */
#pragma once

#include "platform.hpp"
#include "simulator_config.hpp"

class SimulatorButtons : public IButtons
{
    public:
        /** Width of the button strip, in pixels, added to the window's
         * width beyond the emulated panel. Zero on a product with no
         * buttons (SIM_HAS_BUTTONS), which is what keeps that product's
         * window exactly panel-sized and its renderer chrome-free. */
        static constexpr int CHROME_WIDTH = SIM_HAS_BUTTONS ? 88 : 0;

        /** Two on a product with buttons (one of which may go unpopulated
         * -- see the build plan's action map), zero otherwise. */
        static constexpr uint8_t BUTTON_COUNT = SIM_HAS_BUTTONS ? 2 : 0;

        struct Rect
        {
                int x = 0;
                int y = 0;
                int w = 0;
                int h = 0;
        };

        static SimulatorButtons &instance();

        void    begin() override;
        uint8_t count() const override;
        bool    is_down(uint8_t idx) const override;

        /**
         * Where button `idx` sits in window coordinates, given the emulated
         * panel size. Buttons are stacked vertically, centred as a group in
         * the chrome strip, and shrink to fit a short panel.
         *
         * @returns A zero-sized Rect for an out-of-range index.
         */
        Rect rect_for(uint8_t idx, int display_w, int display_h) const;

        /**
         * Current pressed state, with no side effects: no event pump, no
         * repaint.
         *
         * SimulatorDisplay::draw_chrome() must use this rather than
         * is_down(), which repaints on an edge -- and repainting is what
         * draws the chrome. Calling is_down() from there would recurse.
         */
        bool pressed_state(uint8_t idx) const;

    private:
        /**
         * Presents a fresh frame when a button's state has just changed,
         * so a press is visible even on a screen LVGL is not repainting. */
        void repaint_if_changed() const;
};
