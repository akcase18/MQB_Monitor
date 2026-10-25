/**
 * @file dash_s3_display.hpp
 * @brief IDisplay for the dash's ILI9488 panel over the ESP32-S3's LCD_CAM
 * peripheral in 8-bit i80 mode, via ESP-IDF's esp_lcd driver.
 *
 * Offers LVGL two whole-screen frame buffers in PSRAM (get_frame_buffers)
 * so a render pass reaches the panel as one DMA transfer, started on the
 * panel's tearing-effect pulse rather than whenever LVGL happens to
 * finish a band. See the flush()/flush_end() comments in the .cpp.
 */
#pragma once

#include "platform.hpp"

class DashS3Display : public IDisplay
{
    public:
        static DashS3Display &instance();

        void begin() override;
        void set_orientation(DisplayOrientation orientation,
                             bool               flipped) override;
        void get_resolution(uint16_t &width, uint16_t &height) const override;
        void flush(int32_t         x1,
                   int32_t         y1,
                   int32_t         x2,
                   int32_t         y2,
                   const uint16_t *pixels) override;
        bool get_frame_buffers(uint16_t *&buf_1,
                               uint16_t *&buf_2,
                               size_t    &bytes) override;
        void flush_end() override;
        void set_backlight(uint8_t pct) override;
        void set_backlight_off() override;

    private:
        DashS3Display() = default;

        uint16_t width_  = 480;
        uint16_t height_ = 320;
};
