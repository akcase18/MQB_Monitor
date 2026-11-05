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
 * PROGMEM is the data counterpart: a const table left in flash, which the
 * Teensy 4 reads directly (no pgm_read_*()). Without it, const data is
 * copied into DTCM (RAM1) like any variable. Also a no-op off Teensy.
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
    #ifndef PROGMEM
        #define PROGMEM
    #endif

    #include <esp_heap_caps.h>
    #include <new>
    #include <stddef.h>
    #include <stdlib.h>
    #include <type_traits>

/**
 * ESP32: memory from PSRAM where the chip has it (the S3), else the
 * ordinary heap (the C3). Asks for PSRAM outright, so unlike a plain
 * malloc it doesn't depend on the S3's 2KB redirect
 * (dash_s3_platform.cpp) or on being under it. Null if neither has room.
 */
inline void *psram_malloc(size_t bytes)
{
    return heap_caps_malloc_prefer(
        bytes, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT);
}

/**
 * ESP32 stand-in for a large DMAMEM buffer: allocated with psram_malloc()
 * on first get() and kept for good. A static array lands in internal RAM,
 * which the S3's Wi-Fi stack draws on and runs short of (see env:dash_s3
 * in platformio.ini). get() is null if the allocation failed.
 */
template <typename T, size_t N>
class LazyBuffer
{
        static_assert(std::is_trivial<T>::value,
                      "LazyBuffer hands out raw memory -- use psram_new() "
                      "for a type that needs constructing");

    public:
        T *get()
        {
            if (buf_ == nullptr)
            {
                buf_ = static_cast<T *>(psram_malloc(N * sizeof(T)));
            }
            return buf_;
        }

        static constexpr size_t size()
        {
            return N * sizeof(T);
        }

    private:
        T *buf_ = nullptr;
};

/**
 * ESP32 stand-in for a DMAMEM object: one T, constructed from `args` in
 * memory from psram_malloc() and never freed. For objects needed from
 * boot, where a null check at every use would buy nothing: if even this
 * can't be allocated the heap is already gone, so it aborts, which logs a
 * backtrace on the console.
 */
template <typename T, typename... Args>
T &psram_new(Args &&...args)
{
    void *p = psram_malloc(sizeof(T));
    if (p == nullptr)
    {
        abort();
    }
    return *new (p) T(static_cast<Args &&>(args)...);
}
#elif defined(ARDUINO)
    #include <avr/pgmspace.h>
#else
    #define FLASHMEM
    #define DMAMEM
    #define PROGMEM
#endif
