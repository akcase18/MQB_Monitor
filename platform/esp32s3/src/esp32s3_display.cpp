#include "esp32s3_display.hpp"

#include "esp32s3_pins.hpp"

#include <Arduino.h>
#include <SPI.h>

namespace
{
    /* ST7789 command set, only the ones this driver sends. */
    constexpr uint8_t CMD_SWRESET = 0x01;
    constexpr uint8_t CMD_SLPOUT  = 0x11;
    constexpr uint8_t CMD_NORON   = 0x13;
    constexpr uint8_t CMD_INVON   = 0x21;
    constexpr uint8_t CMD_DISPON  = 0x29;
    constexpr uint8_t CMD_CASET   = 0x2A;
    constexpr uint8_t CMD_RASET   = 0x2B;
    constexpr uint8_t CMD_RAMWR   = 0x2C;
    constexpr uint8_t CMD_MADCTL  = 0x36;
    constexpr uint8_t CMD_COLMOD  = 0x3A;

    /**
     * MADCTL bits. MV/MX/MY select which way the controller walks its own
     * frame memory, which is what "rotation" is on this part -- the panel
     * itself does not move. */
    constexpr uint8_t MADCTL_MY = 0x80;
    constexpr uint8_t MADCTL_MX = 0x40;
    constexpr uint8_t MADCTL_MV = 0x20;

    /** 16 bits per pixel. */
    constexpr uint8_t COLMOD_RGB565 = 0x55;

    /**
     * 40MHz. Conservative for an S3 (the part will take 80) and chosen to
     * survive a few centimetres of ribbon and a connector rather than to
     * win a benchmark: a 320x172 panel at 20Hz needs about 2.2MB/s, so
     * this is an order of magnitude of headroom. Raise it once a real
     * board is in hand and the picture is stable. */
    constexpr uint32_t SPI_HZ = 40000000;

    /**
     * LEDC (the ESP32's PWM peripheral) for the backlight. Channel 0 is
     * free -- nothing else in this firmware uses LEDC. 8-bit depth is 256
     * steps against a 0-100 setting, so every percent is distinguishable.
     * 5kHz is above audible and well inside what an LED driver follows. */
    constexpr uint8_t  BL_LEDC_CHANNEL    = 0;
    constexpr uint32_t BL_LEDC_FREQ_HZ    = 5000;
    constexpr uint8_t  BL_LEDC_RESOLUTION = 8;
    constexpr uint32_t BL_LEDC_MAX_DUTY   = (1u << BL_LEDC_RESOLUTION) - 1u;

    /**
     * Pixels swapped and pushed per SPI transaction. 256 pixels is 512
     * bytes of stack -- small enough to be free, large enough that the
     * per-transaction overhead disappears against the transfer. */
    constexpr size_t SWAP_CHUNK_PIXELS = 256;

    SPIClass &bus()
    {
        /**
         * FSPI, the S3's general-purpose host. HSPI is the other one; SPI2
         * is wired to the flash and is not ours. */
        static SPIClass s_spi(FSPI);
        return s_spi;
    }
} // namespace

Esp32s3Display &Esp32s3Display::instance()
{
    static Esp32s3Display s_instance;
    return s_instance;
}

void Esp32s3Display::write_command(uint8_t        cmd,
                                   const uint8_t *params,
                                   size_t         len)
{
    digitalWrite(ESP32S3_PIN_LCD_CS, LOW);

    digitalWrite(ESP32S3_PIN_LCD_DC, LOW);
    bus().transfer(cmd);

    if (params != nullptr && len > 0)
    {
        digitalWrite(ESP32S3_PIN_LCD_DC, HIGH);
        bus().writeBytes(params, len);
    }

    /**
     * One window, opened before the command and closed after the last
     * parameter -- see the header for what happens otherwise. */
    digitalWrite(ESP32S3_PIN_LCD_CS, HIGH);
}

void Esp32s3Display::begin()
{
    if (began_)
    {
        return;
    }
    began_ = true;

    pinMode(ESP32S3_PIN_LCD_DC, OUTPUT);
    pinMode(ESP32S3_PIN_LCD_CS, OUTPUT);
    pinMode(ESP32S3_PIN_LCD_RST, OUTPUT);
    digitalWrite(ESP32S3_PIN_LCD_CS, HIGH);

    /**
     * The backlight comes up *off* and stays off until something has
     * been rendered. app::backlight owns when that is; all this driver
     * promises is that power-on garbage in frame memory is never
     * visible. The Teensy driver makes the same promise the same way. */
    ledcSetup(BL_LEDC_CHANNEL, BL_LEDC_FREQ_HZ, BL_LEDC_RESOLUTION);
    ledcAttachPin(ESP32S3_PIN_LCD_BL, BL_LEDC_CHANNEL);
    ledcWrite(BL_LEDC_CHANNEL, 0);

    bus().begin(ESP32S3_PIN_LCD_SCLK,
                -1, /* MISO -- the panel is write-only */
                ESP32S3_PIN_LCD_MOSI,
                -1 /**
                    * CS handled by hand, so a command and its data can
                    * share one assertion window */);
    bus().beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));

    /**
     * Hardware reset. The datasheet wants 10us low; 20ms either side is
     * free at boot and covers a slow RC on the reset line. */
    digitalWrite(ESP32S3_PIN_LCD_RST, HIGH);
    delay(20);
    digitalWrite(ESP32S3_PIN_LCD_RST, LOW);
    delay(20);
    digitalWrite(ESP32S3_PIN_LCD_RST, HIGH);
    delay(120);

    write_command(CMD_SWRESET);
    delay(150);

    write_command(CMD_SLPOUT);
    delay(120);

    const uint8_t colmod = COLMOD_RGB565;
    write_command(CMD_COLMOD, &colmod, 1);

    /**
     * Inversion ON is correct for essentially every ST7789 module on the
     * market -- the panels are normally-black and the controller's
     * "normal" mode assumes normally-white. Without it the picture is a
     * photographic negative, which is unmistakable and easy to fix; it is
     * called out because it looks like a bug in the theme. */
    write_command(CMD_INVON);

    write_command(CMD_NORON);
    delay(10);

    set_orientation(orientation_);

    write_command(CMD_DISPON);
    delay(120);
}

void Esp32s3Display::set_orientation(DisplayOrientation orientation)
{
    orientation_ = orientation;

    /**
     * MV swaps the controller's row and column axes. The GRAM offset
     * follows it: the 240-wide axis is the short one, so whichever screen
     * axis that becomes is the one that needs shifting by 34.
     *
     * Portrait is supported because the interface has it, not because
     * this product uses it -- a 320x172 strip in a dashboard is landscape
     * or it is nothing. src/main_gauge.cpp never asks for anything else. */
    uint8_t madctl = 0;

    if (orientation == DisplayOrientation::Landscape)
    {
        madctl    = MADCTL_MV | MADCTL_MX;
        width_    = PANEL_LONG_SIDE;
        height_   = PANEL_SHORT_SIDE;
        x_offset_ = 0;
        y_offset_ = GRAM_OFFSET;
    }
    else
    {
        madctl    = 0;
        width_    = PANEL_SHORT_SIDE;
        height_   = PANEL_LONG_SIDE;
        x_offset_ = GRAM_OFFSET;
        y_offset_ = 0;
    }

    if (!began_)
    {
        return;
    }

    write_command(CMD_MADCTL, &madctl, 1);
}

void Esp32s3Display::get_resolution(uint16_t &width, uint16_t &height) const
{
    width  = width_;
    height = height_;
}

void Esp32s3Display::set_window(uint16_t x1,
                                uint16_t y1,
                                uint16_t x2,
                                uint16_t y2)
{
    uint16_t cx1 = x1 + x_offset_;
    uint16_t cx2 = x2 + x_offset_;
    uint16_t cy1 = y1 + y_offset_;
    uint16_t cy2 = y2 + y_offset_;

    uint8_t caset[4] = { static_cast<uint8_t>(cx1 >> 8),
                         static_cast<uint8_t>(cx1 & 0xFF),
                         static_cast<uint8_t>(cx2 >> 8),
                         static_cast<uint8_t>(cx2 & 0xFF) };
    write_command(CMD_CASET, caset, sizeof(caset));

    uint8_t raset[4] = { static_cast<uint8_t>(cy1 >> 8),
                         static_cast<uint8_t>(cy1 & 0xFF),
                         static_cast<uint8_t>(cy2 >> 8),
                         static_cast<uint8_t>(cy2 & 0xFF) };
    write_command(CMD_RASET, raset, sizeof(raset));
}

void Esp32s3Display::flush(int32_t         x1,
                           int32_t         y1,
                           int32_t         x2,
                           int32_t         y2,
                           const uint16_t *pixels)
{
    if (!began_ || pixels == nullptr || x2 < x1 || y2 < y1)
    {
        return;
    }

    set_window(static_cast<uint16_t>(x1),
               static_cast<uint16_t>(y1),
               static_cast<uint16_t>(x2),
               static_cast<uint16_t>(y2));

    size_t count = static_cast<size_t>(x2 - x1 + 1)
                   * static_cast<size_t>(y2 - y1 + 1);

    /**
     * RAMWR and every pixel that follows it are one chip-select window,
     * for the same reason a command and its parameters are -- so this
     * does not go through write_command(). */
    digitalWrite(ESP32S3_PIN_LCD_CS, LOW);
    digitalWrite(ESP32S3_PIN_LCD_DC, LOW);
    bus().transfer(CMD_RAMWR);
    digitalWrite(ESP32S3_PIN_LCD_DC, HIGH);

    /**
     * Swapped into a chunk rather than in place: `pixels` is LVGL's draw
     * buffer and belongs to LVGL. See the header for why the swap is
     * needed at all. */
    uint8_t chunk[SWAP_CHUNK_PIXELS * 2];
    size_t  sent = 0;

    while (sent < count)
    {
        size_t n = count - sent;
        if (n > SWAP_CHUNK_PIXELS)
        {
            n = SWAP_CHUNK_PIXELS;
        }

        for (size_t i = 0; i < n; i++)
        {
            uint16_t px         = pixels[sent + i];
            chunk[(i * 2) + 0]  = static_cast<uint8_t>(px >> 8);
            chunk[(i * 2) + 1]  = static_cast<uint8_t>(px & 0xFF);
        }

        bus().writeBytes(chunk, n * 2);
        sent += n;
    }

    digitalWrite(ESP32S3_PIN_LCD_CS, HIGH);
}

void Esp32s3Display::set_backlight(uint8_t pct)
{
    if (pct > 100)
    {
        pct = 100;
    }

    uint32_t duty = (BL_LEDC_MAX_DUTY * pct) / 100u;
    ledcWrite(BL_LEDC_CHANNEL, duty);
}

void Esp32s3Display::set_backlight_off()
{
    /**
     * A true off, which set_backlight(0) deliberately is not -- see
     * IDisplay. On this driver they happen to produce the same duty, but
     * the distinction belongs to the interface and the two call sites
     * mean different things. */
    ledcWrite(BL_LEDC_CHANNEL, 0);
}
