/**
 * @file mem_placement.hpp
 * @brief FLASHMEM/DMAMEM, the two Teensyduino attributes for keeping cold
 * code and large buffers out of RAM1.
 *
 * On the Teensy 4 the linker puts *all* code in ITCM (RAM1) by default,
 * because that's the fastest place to run it from. RAM1 is only 512KB and
 * is shared between code, every static/global variable, and the stack --
 * and LVGL alone accounts for ~300KB of code. Anything that runs rarely
 * therefore has no business being there: FLASHMEM leaves a function in
 * flash (16MB, almost entirely unused here) to be fetched through the
 * instruction cache, and DMAMEM puts a buffer in RAM2/OCRAM (512KB, also
 * nearly untouched) instead.
 *
 * Both are no-ops off Teensy, so a file can be annotated unconditionally
 * and still build for the native simulator or the ESP32-C3 relay.
 *
 * Use FLASHMEM for code that runs at boot or in response to a user
 * interaction -- screen construction, config load/save, the wireless
 * link. Do NOT use it for anything on a per-frame or per-CAN-frame path
 * (PanelWidget::set_view_model, UdsClient::poll, Scheduler::tick,
 * IDisplay::flush): those want ITCM's deterministic single-cycle fetch.
 */
#pragma once

#if defined(ARDUINO_ARCH_ESP32)
    #define FLASHMEM
    #define DMAMEM
#elif defined(ARDUINO)
    #include <avr/pgmspace.h>
#else
    #define FLASHMEM
    #define DMAMEM
#endif
