/**
 * @file dash_s3_display.cpp
 * @brief ILI9488 over the S3's i80 bus, written directly against esp_lcd's
 * panel-IO layer rather than a display library. The driver *is* a bus
 * setup, an init sequence, a window-set and a DMA pixel push; a library
 * would hide the three things that actually differ per panel (init
 * registers, MADCTL bits, byte order) behind configuration flags that are
 * easy to get wrong and hard to see.
 *
 * All sequences are copied from the ILI948x_t4_mm library (in
 * lib/ILI948x_t4_mm).
 *
 * There are 2 different drawing methods in this file:
 *
 * - Banded (LVGL PARTIAL mode, main.cpp's fallback): flush() pushes each
 *   band as it is rendered. Ten render-then-DMA steps per full screen,
 *   each landing whenever it lands relative to the panel's own scan, so
 *   a screen-wide change (a warning tripping) shows as a wipe with tear
 *   lines where the scan crossed a band mid-write.
 * - Whole-frame (LVGL DIRECT mode, the default): LVGL renders into one
 *   of two full-screen buffers in PSRAM, flush() only notes which rows
 *   changed, and flush_end() hands the union of those rows to a push
 *   task that starts the single DMA on the panel's tearing-effect pulse.
 *   Render time never shows, and the write chases the scan from the top
 *   of the blanking interval, so it never crosses it.
 */

#include "dash_s3_display.hpp"

#include <Arduino.h>

#include <initializer_list>

#include "dash_s3_pins.hpp"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace
{
    constexpr uint16_t LCD_HORZ = 480;
    constexpr uint16_t LCD_VERT = 320;

    /**
     * ILI9488 command set, only what this file sends.
     */
    constexpr uint8_t CMD_SWRESET = 0x01;
    constexpr uint8_t CMD_SLPOUT  = 0x11;
    constexpr uint8_t CMD_DISPOFF = 0x28;
    constexpr uint8_t CMD_DISPON  = 0x29;
    constexpr uint8_t CMD_CASET   = 0x2A;
    constexpr uint8_t CMD_PASET   = 0x2B;
    constexpr uint8_t CMD_RAMWR   = 0x2C;
    constexpr uint8_t CMD_TEON    = 0x35;
    constexpr uint8_t CMD_MADCTL  = 0x36;
    constexpr uint8_t CMD_COLMOD  = 0x3A;

    /**
     * MADCTL bits.
     */
    constexpr uint8_t MADCTL_MY  = 0x80;
    constexpr uint8_t MADCTL_MX  = 0x40;
    constexpr uint8_t MADCTL_MV  = 0x20;
    constexpr uint8_t MADCTL_BGR = 0x08;

    /**
     * ILI948x_t4_mm's MADCTL_ARRAY indexed by its rotation number, and the
     * two entries the Teensy driver uses: setRotation(3) for landscape
     * and setRotation(0) for portrait (teensy_display.cpp,
     * ROTATION_LANDSCAPE / ROTATION_PORTRAIT). Rotation 3 is the confirmed
     * landscape mounting; portrait 0 is correct for the v3 PCB and would
     * flip to 2 (MY|BGR) if the panel were ever mounted the other way up.
     *
     * Changing either REQUIRES a matching change in dash_s3_touch.cpp:
     * flipping the panel 180 degrees inverts both screen axes, so the
     * touch transform inverts with it. The two are only correct together.
     */
    constexpr uint8_t MADCTL_LANDSCAPE
        = MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR;       /* 0xE8 */
    constexpr uint8_t MADCTL_PORTRAIT = MADCTL_MX | MADCTL_BGR; /* 0x48 */

    /**
     * The same two mountings turned 180 degrees (IDisplay::
     * set_orientation's `flipped`) -- MX and MY both toggled, i.e. the
     * Teensy's setRotation(1) and setRotation(2). Touch follows via
     * flip_touch_point() rather than a new transform.
     */
    constexpr uint8_t MADCTL_LANDSCAPE_FLIPPED
        = MADCTL_LANDSCAPE ^ (MADCTL_MX | MADCTL_MY); /* 0x28 */
    constexpr uint8_t MADCTL_PORTRAIT_FLIPPED
        = MADCTL_PORTRAIT ^ (MADCTL_MX | MADCTL_MY); /* 0x88 */

    /**
     * Whole-frame rendering (see the file comment) only pays for itself
     * when the push can wait for TE: without it the push lands mid-scan
     * and tears anyway, while rendering into PSRAM and DMAing out of it
     * costs real time. On the PCB (no TE, quad PSRAM) a full redraw took
     * ~57ms whole-frame against ~33-46ms banded from internal SRAM with
     * 1/5-screen bands (DRAW_BUF_DIVISOR in platformio.ini). So it
     * follows the TE pin by default; set 0/1 to A/B on the bench.
     */
#ifndef DASH_S3_FRAME_BUFFERS
    #define DASH_S3_FRAME_BUFFERS (DASH_S3_PIN_LCD_TE >= 0)
#endif

    /**
     * Pixel-clock, i.e. WR-strobe, rate. In whole-frame mode the limit
     * is PSRAM bandwidth, not the panel: the LCD DMA reads the frame out
     * of PSRAM while LVGL renders and syncs the other buffer there too.
     * On the PCB's N16R2 (quad PSRAM, ~40MB/s at 80MHz) 20MHz and 15MHz
     * both starved the DMA -- rows silently kept stale content until
     * something redrew them -- and 10MHz is clean; the N16R8 dev kit's
     * octal PSRAM runs 20MHz fine. Banded mode DMAs from internal SRAM,
     * so it has no such limit: 20MHz is the ILI9488's rated ~50ns write
     * cycle (the Teensy runs 40MHz on its ribbon, out of spec).
     *
     * At 10MHz a full 480x320 RGB565 frame (307,200 bytes, one byte per
     * strobe) is ~31ms of DMA; at 20MHz ~15ms, matching the Teensy.
     */
#ifndef DASH_S3_LCD_BUS_HZ
    #if DASH_S3_FRAME_BUFFERS
        #define DASH_S3_LCD_BUS_HZ (10 * 1000 * 1000)
    #else
        #define DASH_S3_LCD_BUS_HZ (20 * 1000 * 1000)
    #endif
#endif
    constexpr uint32_t LCD_BUS_HZ = DASH_S3_LCD_BUS_HZ;

    /**
     * One whole screen of RGB565, whichever way round it is mounted.
     */
    constexpr size_t FRAME_BYTES
        = static_cast<size_t>(LCD_HORZ) * LCD_VERT * sizeof(uint16_t);

    /**
     * How long the push task waits for a TE edge before sending anyway.
     * The ILI9488 refreshes at ~60Hz on its power-on frame-rate setting
     * (0xB1 is left at default), so this is three frames: long enough
     * that a real pulse is never missed, short enough that a TE wire
     * that isn't there (or DASH_S3_PIN_LCD_TE -1) costs a visible stall
     * per pass rather than a hang. Logged once if it happens.
     */
    constexpr uint32_t TE_TIMEOUT_MS = 50;

    /**
     * The push task: tiny stack (it makes two esp_lcd calls), above
     * loopTask's priority 1 so a TE edge is acted on within microseconds
     * of the ISR rather than after LVGL's current draw call, and on the
     * Arduino core so it never contends with Wi-Fi on the other one.
     */
    constexpr uint32_t    PUSH_TASK_STACK = 3072;
    constexpr UBaseType_t PUSH_TASK_PRIO  = 3;

    /**
     * Backlight PWM. On the S3 PCB this pin drives the PAM2804's EN
     * input. Unlike the Teensy board's TPS61169 (which filters its CTRL
     * duty into an analog reference, and wants a 5kHz-100kHz carrier),
     * EN is a plain enable: PWM on it gates the buck on and off, and
     * every rising edge restarts the part through its soft-start. The
     * datasheet's PWM-dimming figure is 500Hz (DS36403 "Application
     * Information"); a 20kHz carrier has a 50us period, too short for the
     * output to ever come up, so the LEDs stay dark. 500Hz is above
     * visible flicker. 10-bit resolution gives finer steps at the dim
     * end, where the soft-start eats into each on-pulse, and is trivial
     * for the LEDC timer (500Hz * 1024 = 512kHz).
     */
    constexpr uint8_t  BL_LEDC_CHANNEL = 0;
    constexpr uint32_t BL_PWM_FREQ_HZ  = 500;
    constexpr uint8_t  BL_PWM_BITS     = 10;
    constexpr uint32_t BL_PWM_MAX      = (1u << BL_PWM_BITS) - 1;

    /**
     * Hardware reset pulse, as ILI948x_t4_mm::begin() does it.
     */
    constexpr uint32_t RESET_HIGH_MS   = 15;
    constexpr uint32_t RESET_LOW_MS    = 15;
    constexpr uint32_t RESET_SETTLE_MS = 100;

    esp_lcd_i80_bus_handle_t  s_bus = nullptr;
    esp_lcd_panel_io_handle_t s_io  = nullptr;

    /**
     * Given from the i80 driver's transfer-done ISR, taken at the top of
     * flush() when a transfer is still in flight -- see flush(). Binary,
     * not counting: at most one colour transfer is ever outstanding.
     */
    SemaphoreHandle_t s_trans_done = nullptr;
    bool              s_in_flight  = false;

    /**
     * Whole-frame mode state. s_direct is set once get_frame_buffers()
     * has handed the PSRAM buffers to main.cpp; from then on flush()
     * accumulates and flush_end() pushes.
     */
    uint16_t *s_frame_buf[2] = { nullptr, nullptr };
    bool      s_direct       = false;

    /**
     * Rows touched so far this render pass, and the buffer they are in.
     * Owned by the LVGL (loop) task.
     */
    const uint16_t *s_pass_base  = nullptr;
    int32_t         s_pass_y1    = 0;
    int32_t         s_pass_y2    = 0;
    bool            s_pass_dirty = false;

    /**
     * The band handed to the push task. Written by flush_end() only
     * after wait_for_transfer() has confirmed the previous push is
     * finished with it; read by the task once per s_push_request.
     */
    struct PendingPush
    {
            const uint16_t *base;
            int32_t         y1;
            int32_t         y2;
            uint16_t        width; /* row stride, in pixels */
    };
    PendingPush       s_pending      = {};
    SemaphoreHandle_t s_push_request = nullptr;
    SemaphoreHandle_t s_te_edge      = nullptr;
    TaskHandle_t      s_push_task    = nullptr;

/**
 * Once-a-second throughput line on the console: how many flushes landed,
 * how many bytes went over the bus, and how long flush() spent blocked
 * waiting for the previous DMA (total and worst single wait). Answers
 * "is the display actually keeping up" with numbers rather than eyes.
 * Off by default -- it is a Serial.printf in the flush path -- and
 * switched on by env:dash_s3_stress.
 */
#ifndef DASH_S3_LCD_STATS
    #define DASH_S3_LCD_STATS 0
#endif
#if DASH_S3_LCD_STATS
    struct Stats
    {
            uint32_t window_start_ms = 0;
            uint32_t flushes         = 0;
            uint32_t bytes           = 0;
            uint32_t wait_us_total   = 0;
            uint32_t wait_us_max     = 0;
    };
    Stats s_stats;

    void stats_note(uint32_t bytes, uint32_t wait_us)
    {
        s_stats.flushes++;
        s_stats.bytes += bytes;
        s_stats.wait_us_total += wait_us;
        if (wait_us > s_stats.wait_us_max)
        {
            s_stats.wait_us_max = wait_us;
        }

        uint32_t now = millis();
        if (now - s_stats.window_start_ms >= 1000)
        {
            Serial.printf(
                "LCD: %lu flushes/s, %lu KB/s, dma-wait %lu ms "
                "total, %lu us worst\n",
                static_cast<unsigned long>(s_stats.flushes),
                static_cast<unsigned long>(s_stats.bytes / 1024),
                static_cast<unsigned long>(s_stats.wait_us_total / 1000),
                static_cast<unsigned long>(s_stats.wait_us_max));
            s_stats                 = Stats {};
            s_stats.window_start_ms = now;
        }
    }
#endif

    bool IRAM_ATTR trans_done_cb(esp_lcd_panel_io_handle_t,
                                 esp_lcd_panel_io_event_data_t *,
                                 void *)
    {
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(s_trans_done, &woken);
        return woken == pdTRUE;
    }

    void ARDUINO_ISR_ATTR te_isr(void *)
    {
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(s_te_edge, &woken);
        if (woken == pdTRUE)
        {
            portYIELD_FROM_ISR();
        }
    }

    /**
     * @returns Microseconds spent blocked, for the stats line.
     */
    uint32_t wait_for_transfer()
    {
        if (!s_in_flight)
        {
            return 0;
        }
        uint32_t t0 = micros();
        xSemaphoreTake(s_trans_done, portMAX_DELAY);
        s_in_flight = false;
        return micros() - t0;
    }

    void cmd(uint8_t command, const uint8_t *params, size_t len)
    {
        if (s_io == nullptr)
        {
            /**
             * begin() failed and said so; every later call is a no-op so
             * the rest of the firmware keeps running headless.
             */
            return;
        }

        /**
         * Never while a colour transfer is in flight: the parameter path
         * is synchronous and would interleave with the queued DMA.
         */
        wait_for_transfer();
        esp_err_t err = esp_lcd_panel_io_tx_param(s_io, command, params, len);
        if (err != ESP_OK)
        {
            Serial.printf("ILI9488: cmd 0x%02X failed: %s\n",
                          command,
                          esp_err_to_name(err));
        }
    }

    void cmd(uint8_t command)
    {
        cmd(command, nullptr, 0);
    }

    void cmd(uint8_t command, std::initializer_list<uint8_t> params)
    {
        cmd(command, params.begin(), params.size());
    }

    void init_panel()
    {
        /**
         * ILI948x_t4_mm.cpp, begin(), the #if defined(ILI9488) block --
         * same registers, same values, same delays.
         */
        cmd(CMD_SWRESET);
        delay(120);
        cmd(CMD_SLPOUT);
        delay(15);
        cmd(CMD_DISPOFF);
        delay(15);

        cmd(0xC0, { 0x19, 0x1A });       /* Power Control 1 */
        cmd(0xC1, { 0x45, 0x00 });       /* Power Control 2 */
        cmd(0xC2, { 0x33 });             /* Power Control 3 */
        cmd(0xC5, { 0x00, 0x12, 0x80 }); /* VCOM Control */
        cmd(0xB4, { 0x02 });             /* Display Inversion Control */
        cmd(0xB6, { 0x00, 0x02, 0x3B }); /* Display Function Control */
        cmd(0xB7, { 0x07 });             /* Entry Mode Set */
        cmd(0xE0,
            { 0x00,
              0x03,
              0x09,
              0x08,
              0x16,
              0x0A,
              0x3F,
              0x78,
              0x4C,
              0x09,
              0x0A,
              0x08,
              0x16,
              0x1A,
              0x0F }); /* Positive Gamma */
        cmd(0xE1,
            { 0x00,
              0x16,
              0x19,
              0x03,
              0x0F,
              0x05,
              0x32,
              0x45,
              0x46,
              0x04,
              0x0E,
              0x0D,
              0x35,
              0x37,
              0x0F }); /* Negative Gamma */
        cmd(CMD_MADCTL, { MADCTL_PORTRAIT });
        /**
         * Tearing-effect line on, V-blank pulse only (mode 0). Costs
         * nothing if the pin isn't wired.
         */
        cmd(CMD_TEON, { 0x00 });
        cmd(CMD_DISPON);
        delay(120);

        /**
         * setBitDepth(16): RGB565, two bus beats per pixel. The ILI9488
         * only accepts 16bpp on the parallel interface (SPI is 18bpp
         * only), which is one of the reasons this panel is on a parallel
         * bus in the first place.
         */
        cmd(CMD_COLMOD, { 0x55 });
        delay(10);
    }

    /**
     * Deliberately not via cmd(): both callers have already established
     * that nothing is in flight (flush() by its own wait_for_transfer,
     * push_task by construction -- flush_end() waited before handing it
     * anything), and cmd()'s wait would deadlock the push task, which
     * runs while s_in_flight is true by definition.
     */
    void set_window(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
    {
        const uint8_t cols[4] = { static_cast<uint8_t>(x1 >> 8),
                                  static_cast<uint8_t>(x1),
                                  static_cast<uint8_t>(x2 >> 8),
                                  static_cast<uint8_t>(x2) };
        const uint8_t rows[4] = { static_cast<uint8_t>(y1 >> 8),
                                  static_cast<uint8_t>(y1),
                                  static_cast<uint8_t>(y2 >> 8),
                                  static_cast<uint8_t>(y2) };
        esp_lcd_panel_io_tx_param(s_io, CMD_CASET, cols, sizeof(cols));
        esp_lcd_panel_io_tx_param(s_io, CMD_PASET, rows, sizeof(rows));
    }

    /**
     * Starts one colour DMA. On failure, releases the waiter that
     * flush()/flush_end() left behind, since no transfer-done ISR is
     * coming.
     */
    void push_pixels(int32_t         x1,
                     int32_t         y1,
                     int32_t         x2,
                     int32_t         y2,
                     const uint16_t *pixels,
                     size_t          bytes)
    {
        set_window(x1, y1, x2, y2);
        esp_err_t err
            = esp_lcd_panel_io_tx_color(s_io, CMD_RAMWR, pixels, bytes);
        if (err != ESP_OK)
        {
            Serial.printf("ILI9488: flush failed: %s\n", esp_err_to_name(err));
            xSemaphoreGive(s_trans_done);
        }
    }

    /**
     * Whole-frame mode's other half: waits for a band from flush_end(),
     * then for the panel's next TE edge, then issues the DMA. The edge
     * it wants is the *next* one -- one that fired while nothing was
     * pending marks a scan already under way -- so any stale give is
     * drained first.
     */
    void push_task(void *)
    {
        bool te_warned = false;
        for (;;)
        {
            xSemaphoreTake(s_push_request, portMAX_DELAY);
            const PendingPush p = s_pending;

            if (DASH_S3_PIN_LCD_TE >= 0)
            {
                xSemaphoreTake(s_te_edge, 0);
                if (xSemaphoreTake(s_te_edge, pdMS_TO_TICKS(TE_TIMEOUT_MS))
                        != pdTRUE
                    && !te_warned)
                {
                    Serial.printf(
                        "ILI9488: no TE edge on GPIO%d in %lums; "
                        "pushing unsynchronised\n",
                        DASH_S3_PIN_LCD_TE,
                        static_cast<unsigned long>(TE_TIMEOUT_MS));
                    te_warned = true;
                }
            }

            const size_t rows  = static_cast<size_t>(p.y2 - p.y1 + 1);
            const size_t bytes = rows * p.width * sizeof(uint16_t);
            push_pixels(0,
                        p.y1,
                        p.width - 1,
                        p.y2,
                        p.base + static_cast<size_t>(p.y1) * p.width,
                        bytes);
        }
    }
} // namespace

DashS3Display &DashS3Display::instance()
{
    static DashS3Display s_instance;
    return s_instance;
}

void DashS3Display::begin()
{
    /**
     * Park the backlight low before touching the panel at all, and keep
     * it there after init: the panel's GRAM is stale from power-on until
     * the first real frame lands, so it is on the caller (see main.cpp's
     * display_flush_cb) to light the backlight once that has happened.
     * On the PAM2804 a held-low EN is a true shutdown (<1uA).
     */
    pinMode(DASH_S3_PIN_LCD_BL, OUTPUT);
    digitalWrite(DASH_S3_PIN_LCD_BL, LOW);
    ledcSetup(BL_LEDC_CHANNEL, BL_PWM_FREQ_HZ, BL_PWM_BITS);
    ledcAttachPin(DASH_S3_PIN_LCD_BL, BL_LEDC_CHANNEL);
    ledcWrite(BL_LEDC_CHANNEL, 0);

    pinMode(DASH_S3_PIN_LCD_RST, OUTPUT);
    digitalWrite(DASH_S3_PIN_LCD_RST, HIGH);
    delay(RESET_HIGH_MS);
    digitalWrite(DASH_S3_PIN_LCD_RST, LOW);
    delay(RESET_LOW_MS);
    digitalWrite(DASH_S3_PIN_LCD_RST, HIGH);
    delay(RESET_SETTLE_MS);

    s_trans_done = xSemaphoreCreateBinary();

    esp_lcd_i80_bus_config_t bus_cfg = {};
    bus_cfg.dc_gpio_num              = DASH_S3_PIN_LCD_DC;
    bus_cfg.wr_gpio_num              = DASH_S3_PIN_LCD_WR;
    bus_cfg.clk_src                  = LCD_CLK_SRC_PLL160M;
    bus_cfg.data_gpio_nums[0]        = DASH_S3_PIN_LCD_D0;
    bus_cfg.data_gpio_nums[1]        = DASH_S3_PIN_LCD_D1;
    bus_cfg.data_gpio_nums[2]        = DASH_S3_PIN_LCD_D2;
    bus_cfg.data_gpio_nums[3]        = DASH_S3_PIN_LCD_D3;
    bus_cfg.data_gpio_nums[4]        = DASH_S3_PIN_LCD_D4;
    bus_cfg.data_gpio_nums[5]        = DASH_S3_PIN_LCD_D5;
    bus_cfg.data_gpio_nums[6]        = DASH_S3_PIN_LCD_D6;
    bus_cfg.data_gpio_nums[7]        = DASH_S3_PIN_LCD_D7;
    bus_cfg.bus_width                = 8;
    /**
     * A whole frame, so main.cpp can grow its draw bands up to
     * full-frame without this driver caring. The driver sizes its DMA
     * descriptor chain from this: ~75 descriptors, about 1KB.
     */
    bus_cfg.max_transfer_bytes = LCD_HORZ * LCD_VERT * sizeof(uint16_t);
    /**
     * Draw buffers are static arrays in internal SRAM (main.cpp), so
     * the PSRAM alignment is moot but must be non-zero. Anything DMA'd
     * from here must be internal memory -- with SPIRAM_USE_MALLOC, a
     * malloc() over 4KB lands in PSRAM, which the LCD DMA can't fetch
     * from at this alignment. main.cpp's buffers are `alignas(4)`
     * statics in .bss, which is exactly right.
     */
    bus_cfg.psram_trans_align = 64;
    bus_cfg.sram_trans_align  = 4;

    esp_err_t err = esp_lcd_new_i80_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK)
    {
        Serial.printf("ILI9488: i80 bus init failed: %s\n",
                      esp_err_to_name(err));
        return;
    }

    esp_lcd_panel_io_i80_config_t io_cfg = {};
    io_cfg.cs_gpio_num                   = DASH_S3_PIN_LCD_CS;
    io_cfg.pclk_hz                       = LCD_BUS_HZ;
    /**
     * One transfer in flight plus one queued is all flush() ever asks
     * for -- see its comment.
     */
    io_cfg.trans_queue_depth        = 2;
    io_cfg.on_color_trans_done      = trans_done_cb;
    io_cfg.user_ctx                 = nullptr;
    io_cfg.lcd_cmd_bits             = 8;
    io_cfg.lcd_param_bits           = 8;
    io_cfg.dc_levels.dc_idle_level  = 0;
    io_cfg.dc_levels.dc_cmd_level   = 0;
    io_cfg.dc_levels.dc_dummy_level = 0;
    io_cfg.dc_levels.dc_data_level  = 1;
    /**
     * Byte order. LVGL renders RGB565 little-endian, i.e. the low byte
     * of each pixel sits first in memory, while the panel wants the high
     * byte on the first strobe (ILI948x_t4_mm's DMA path shifts out
     * `buf >> 8` then `buf & 0xFF`). The LCD_CAM peripheral swaps
     * adjacent bytes in hardware on the way out, for free. If colours
     * come out subtly wrong (blues/greens smeared, reds washed out),
     * this is the flag to suspect; the software fallback is to run
     * lv_draw_sw_rgb565_swap() over the buffer inside flush().
     */
    io_cfg.flags.swap_color_bytes = 1;

    err = esp_lcd_new_panel_io_i80(s_bus, &io_cfg, &s_io);
    if (err != ESP_OK)
    {
        Serial.printf("ILI9488: i80 panel IO init failed: %s\n",
                      esp_err_to_name(err));
        return;
    }

    init_panel();
    Serial.println("ILI9488 Initialized");
}

void DashS3Display::set_orientation(DisplayOrientation orientation,
                                    bool               flipped)
{
    /**
     * cmd() waits out any in-flight colour transfer first, so a live flip
     * can't interleave with a frame still going out.
     */
    if (orientation == DisplayOrientation::Portrait)
    {
        cmd(CMD_MADCTL,
            { flipped ? MADCTL_PORTRAIT_FLIPPED : MADCTL_PORTRAIT });
        width_  = LCD_VERT;
        height_ = LCD_HORZ;
    }
    else
    {
        cmd(CMD_MADCTL,
            { flipped ? MADCTL_LANDSCAPE_FLIPPED : MADCTL_LANDSCAPE });
        width_  = LCD_HORZ;
        height_ = LCD_VERT;
    }
}

void DashS3Display::get_resolution(uint16_t &width, uint16_t &height) const
{
    width  = width_;
    height = height_;
}

void DashS3Display::flush(
    int32_t x1, int32_t y1, int32_t x2, int32_t y2, const uint16_t *pixels)
{
    if (s_io == nullptr)
    {
        return;
    }

    if (s_direct)
    {
        /**
         * Whole-frame mode: `pixels` is the base of the frame buffer
         * LVGL rendered this pass into, and (x1,y1)-(x2,y2) one of the
         * areas it changed. Rows of a frame buffer are contiguous, so
         * the union of the rows touched is one DMA at full width; x is
         * irrelevant. Nothing moves until flush_end().
         */
        (void)x1;
        (void)x2;
        if (!s_pass_dirty)
        {
            s_pass_y1    = y1;
            s_pass_y2    = y2;
            s_pass_dirty = true;
        }
        else
        {
            s_pass_y1 = y1 < s_pass_y1 ? y1 : s_pass_y1;
            s_pass_y2 = y2 > s_pass_y2 ? y2 : s_pass_y2;
        }
        s_pass_base = pixels;
        return;
    }

    /**
     * Banded mode. Same contract as the Teensy driver's
     * pushPixels16bitDMA: block until any *prior* transfer has
     * completed, then start this one and return -- so returning from
     * here means the flush is issued, not that its pixels have landed.
     * main.cpp calls lv_display_flush_ready synchronously on the
     * strength of exactly this: LVGL alternates its two draw buffers, so
     * the one still being DMA'd out is never the one it renders into
     * next, and the next flush's own entry-wait covers the gap. The
     * frame's last flush is genuinely still in flight on return, which
     * is why main.cpp waits for the display to go quiet before lighting
     * the backlight rather than trusting lv_display_flush_is_last().
     */
    uint32_t wait_us = wait_for_transfer();

    const size_t bytes = static_cast<size_t>(x2 - x1 + 1)
                         * static_cast<size_t>(y2 - y1 + 1) * sizeof(uint16_t);
    s_in_flight        = true;
    push_pixels(x1, y1, x2, y2, pixels, bytes);
#if DASH_S3_LCD_STATS
    stats_note(bytes, wait_us);
#else
    (void)wait_us;
#endif
}

bool DashS3Display::get_frame_buffers(uint16_t *&buf_1,
                                      uint16_t *&buf_2,
                                      size_t    &bytes)
{
    buf_1 = nullptr;
    buf_2 = nullptr;
    bytes = 0;
#if DASH_S3_FRAME_BUFFERS
    if (s_io == nullptr)
    {
        return false; /* headless: keep the banded no-op path */
    }

    /**
     * PSRAM, at the i80 bus's psram_trans_align (64) so the driver will
     * DMA straight out of them; it write-backs the cache itself. Rows
     * are 960 or 640 bytes, both multiples of 64, so any row start is a
     * valid transfer start.
     */
    for (uint16_t *&buf : s_frame_buf)
    {
        buf = static_cast<uint16_t *>(
            heap_caps_aligned_calloc(64, 1, FRAME_BYTES, MALLOC_CAP_SPIRAM));
        if (buf == nullptr)
        {
            Serial.println(
                "ILI9488: PSRAM frame buffers unavailable; "
                "rendering in bands");
            for (uint16_t *&b : s_frame_buf)
            {
                heap_caps_free(b);
                b = nullptr;
            }
            return false;
        }
    }

    s_push_request = xSemaphoreCreateBinary();
    s_te_edge      = xSemaphoreCreateBinary();
    if (DASH_S3_PIN_LCD_TE >= 0)
    {
        pinMode(DASH_S3_PIN_LCD_TE, INPUT);
        attachInterruptArg(DASH_S3_PIN_LCD_TE, te_isr, nullptr, RISING);
    }
    xTaskCreatePinnedToCore(push_task,
                            "lcd_push",
                            PUSH_TASK_STACK,
                            nullptr,
                            PUSH_TASK_PRIO,
                            &s_push_task,
                            CONFIG_ARDUINO_RUNNING_CORE);

    s_direct = true;
    buf_1    = s_frame_buf[0];
    buf_2    = s_frame_buf[1];
    bytes    = FRAME_BYTES;
    return true;
#else
    return false;
#endif
}

void DashS3Display::flush_end()
{
    if (!s_direct || !s_pass_dirty)
    {
        return;
    }
    s_pass_dirty = false;

    /**
     * The wait here is what keeps the two buffers straight: LVGL renders
     * the next pass into the other buffer, which is the one the previous
     * push read from, so that push must be complete -- not merely
     * started -- before this returns. It also means s_pending is free to
     * overwrite: the task copied it before issuing that DMA.
     */
    uint32_t wait_us = wait_for_transfer();

    s_pending.base  = s_pass_base;
    s_pending.y1    = s_pass_y1;
    s_pending.y2    = s_pass_y2;
    s_pending.width = width_;
    s_in_flight     = true;
    xSemaphoreGive(s_push_request);

#if DASH_S3_LCD_STATS
    stats_note(static_cast<uint32_t>(s_pass_y2 - s_pass_y1 + 1) * width_
                   * sizeof(uint16_t),
               wait_us);
#else
    (void)wait_us;
#endif
}

void DashS3Display::set_backlight(uint8_t pct)
{
    if (pct > 100)
    {
        pct = 100;
    }

    /**
     * The user-facing 0-100% maps onto MIN_BACKLIGHT_PCT-100% duty, so
     * "0%" is the dimmest the panel goes rather than off -- the
     * distinction IDisplay::set_backlight_off() exists to draw.
     *
     * The floor keeps the dimmest setting visibly lit: each EN pulse
     * starts with the PAM2804's soft-start, so at a 2ms period the
     * shortest pulses deliver less current than their duty suggests.
     * Raise it if the low end drops to black early, lower it if the
     * dimmest setting is still too bright at night.
     */
    constexpr uint32_t MIN_BACKLIGHT_PCT = 2;
    uint32_t           effective_pct
        = MIN_BACKLIGHT_PCT
          + (static_cast<uint32_t>(pct) * (100 - MIN_BACKLIGHT_PCT)) / 100;

    ledcWrite(BL_LEDC_CHANNEL, (effective_pct * BL_PWM_MAX) / 100);
}

void DashS3Display::set_backlight_off()
{
    /**
     * Deliberately not set_backlight(0), which floors at
     * MIN_BACKLIGHT_PCT and would leave the panel dimly lit -- still
     * bright enough to show the transition this is meant to hide. Duty
     * 0 holds the pin low continuously, which is the PAM2804's
     * shutdown state.
     */
    ledcWrite(BL_LEDC_CHANNEL, 0);
}
