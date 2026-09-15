/**
 * @file simulator_config.hpp
 * @brief Which product the simulator is standing in for.
 *
 * The desktop simulator backs more than one product, and they differ in
 * ways the SDL layer has to know about: panel size, and whether the board
 * has a touchscreen or physical buttons. Rather than an #ifdef per
 * difference scattered through the simulator sources, every one of them is
 * a build flag set by the `[env:...]` in platformio.ini and read here.
 *
 * Defaults describe the original MQB product (480x320, touchscreen, no
 * buttons) so an env that sets nothing gets exactly the behaviour the
 * simulator has always had.
 *
 * This is the simulator's own configuration, not a `platform.hpp`
 * interface -- nothing above platform/ reads it, and no other board has an
 * equivalent. A real board's panel size and button count are facts about
 * the hardware, discovered from its own driver.
 */
#pragma once

/** Emulated panel width in pixels, before any window chrome. */
#ifndef SIM_PANEL_W
    #define SIM_PANEL_W 480
#endif

/** Emulated panel height in pixels. */
#ifndef SIM_PANEL_H
    #define SIM_PANEL_H 320
#endif

/**
 * 1 if this product has physical buttons, drawn as a clickable strip beside
 * the emulated panel. 0 wires NullButtons instead and leaves the window
 * exactly panel-sized.
 *
 * Off for the MQB product: that board has a touchscreen, so buttons would
 * be window furniture for hardware that does not exist.
 */
#ifndef SIM_HAS_BUTTONS
    #define SIM_HAS_BUTTONS 0
#endif

/**
 * 1 if this product reads analog sensors, backed by a scripted engine-warmup
 * model (see simulator_analog_input.cpp). 0 wires NullAnalogInput instead.
 *
 * Off for the MQB product: that board gets its values over CAN, and a
 * simulator offering it four channels of fabricated volts would be modelling
 * hardware it does not have.
 */
#ifndef SIM_HAS_ANALOG
    #define SIM_HAS_ANALOG 0
#endif
