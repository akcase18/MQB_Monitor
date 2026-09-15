#include "lvgl_runtime.hpp"

#include "backlight.hpp"
#include "platform.hpp"

#include <lvgl.h>

namespace app
{
    namespace lvgl_runtime
    {

        namespace
        {
            /**
             * Pixels in each of the two partial-render buffers.
             *
             * Deliberately a fraction of the panel, not a full
             * framebuffer -- LV_DISPLAY_RENDER_MODE_PARTIAL renders and
             * flushes in bands. The default is one tenth of a 480x320
             * panel, which is what this has always been; a product with a
             * smaller panel overrides it from its [env:] rather than
             * carrying 50KB of buffer it cannot fill.
             *
             * A build-flag tunable, not a board branch -- same shape as
             * platform/simulator/inc/simulator_config.hpp, and it names no
             * board.
             */
#ifndef LVGL_DRAW_BUF_PIXELS
    #define LVGL_DRAW_BUF_PIXELS ((480 * 320) / 10)
#endif

            /**
             * RGB565 (2 bytes/pixel), NOT lv_color_t: with LV_COLOR_DEPTH
             * 16 LVGL renders into the draw buffer in the display's own
             * color format, which is what flush_cb() below reinterprets
             * these as. lv_color_t is a 3-byte {b,g,r} struct regardless
             * of LV_COLOR_DEPTH, so declaring them as an array of it
             * over-allocated every buffer by 50% without LVGL ever using
             * the extra byte per pixel. 4-byte aligned for the DMA push in
             * TeensyDisplay::flush(). */
            alignas(4) uint16_t s_draw_buf_1[LVGL_DRAW_BUF_PIXELS];
            alignas(4) uint16_t s_draw_buf_2[LVGL_DRAW_BUF_PIXELS];

            /**
             * How often LVGL polls the touch driver. lv_indev_create()
             * defaults this to LV_DEF_REFR_PERIOD (20ms, lib/lv_conf.h),
             * which is the screen redraw cadence rather than anything
             * chosen for input -- so a press could sit up to 20ms before
             * LVGL saw it, and a drag sampled that coarsely tracks
             * visibly behind the finger.
             *
             * 10ms halves the worst-case press latency and doubles the
             * sample density along a drag, for one extra I2C transaction
             * per 10ms (5 bytes off the FT6236). loop() calls
             * lv_timer_handler() every pass with no rate limit, so there
             * is headroom for it.
             */
            constexpr uint32_t TOUCH_POLL_MS = 10;

            void flush_cb(lv_display_t    *disp,
                          const lv_area_t *area,
                          uint8_t         *px_map)
            {
                platform_get().display.flush(
                    area->x1,
                    area->y1,
                    area->x2,
                    area->y2,
                    reinterpret_cast<const uint16_t *>(px_map));
                lv_display_flush_ready(disp);

                backlight::note_flush(lv_display_flush_is_last(disp));
            }

            void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
            {
                (void)indev;

                /* Also pumps the simulator's SDL event queue. */
                TouchPoint point {};
                bool       pressed = platform_get().touch.read(point);

                data->point.x = point.x;
                data->point.y = point.y;
                data->state   = pressed ? LV_INDEV_STATE_PRESSED
                                        : LV_INDEV_STATE_RELEASED;
            }

            uint32_t tick_cb()
            {
                return platform_get().clock.millis();
            }
        } // namespace

        void begin(const Options &options)
        {
            lv_init();
            lv_tick_set_cb(tick_cb);

            lv_display_t *display
                = lv_display_create(options.width, options.height);
            lv_display_set_flush_cb(display, flush_cb);
            lv_display_set_buffers(display,
                                   s_draw_buf_1,
                                   s_draw_buf_2,
                                   sizeof(s_draw_buf_1),
                                   LV_DISPLAY_RENDER_MODE_PARTIAL);

            if (!options.with_pointer_indev)
            {
                return;
            }

            lv_indev_t *indev = lv_indev_create();
            lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(indev, touch_read_cb);
            lv_timer_set_period(lv_indev_get_read_timer(indev), TOUCH_POLL_MS);
        }

    } // namespace lvgl_runtime
} // namespace app
