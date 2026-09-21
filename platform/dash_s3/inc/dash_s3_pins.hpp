/**
 * @file dash_s3_pins.hpp
 * @brief Every pin this board uses, in one place, and every one of them
 * overridable from platformio.ini.
 *
 * **The pinout is not frozen.** Today the "board" is an ESP32-S3-DevKitC-1
 * (N16R8) on a bench with the dash's panel, touch controller and CAN
 * transceiver wired to its headers. So none of these is written into a
 * driver: each is a `-D` an `[env:]` can set, and the defaults below are
 * a coherent starting set laid out for that dev kit's header order rather
 * than a commitment. Changing one is a line in `platformio.ini`, not an
 * edit to a driver.
 *
 * ## Why these defaults
 *
 * The ESP32-S3 has a lot of GPIOs and several traps. Avoided here:
 *
 * | Pins | Why not |
 * |---|---|
 * | 0, 3, 45, 46 | Strapping pins -- sampled at reset, so a pull on one changes boot mode |
 * | 19, 20 | Native USB D-/D+ |
 * | 26-32 | SPI flash bus |
 * | 33-37 | Octal PSRAM bus on the R8 module. This is why the panel runs 8 data lines, not 16 |
 * | 43, 44 | UART0, the console this firmware logs to (the dev kit's "UART" USB port) |
 * | 38, 48 | The dev kit's RGB LED (GPIO38 on v1.0, GPIO48 on v1.1) -- left spare, not driven |
 *
 * There is no companion-link UART: the S3's own Wi-Fi runs the relay
 * in-process (dash_s3_wireless.cpp). 39, 40 and 47 are the unassigned
 * GPIOs after the above; 47 takes the panel's TE output.
 *
 * The LCD data lines take eight consecutive positions on the dev kit's J1
 * header (4-7, 15-18) so the ribbon can be a straight run.
 */
#pragma once

/**
 * ----------------------------------------------------------------------
 * Display -- ILI9488 over an 8-bit 8080 parallel bus (LCD_CAM i80).
 * Write-only: RD is tied to 3V3 on the panel side, as on the Teensy.
 *---------------------------------------------------------------------*/

#ifndef DASH_S3_PIN_LCD_D0
    #define DASH_S3_PIN_LCD_D0 4
#endif
#ifndef DASH_S3_PIN_LCD_D1
    #define DASH_S3_PIN_LCD_D1 5
#endif
#ifndef DASH_S3_PIN_LCD_D2
    #define DASH_S3_PIN_LCD_D2 6
#endif
#ifndef DASH_S3_PIN_LCD_D3
    #define DASH_S3_PIN_LCD_D3 7
#endif
#ifndef DASH_S3_PIN_LCD_D4
    #define DASH_S3_PIN_LCD_D4 15
#endif
#ifndef DASH_S3_PIN_LCD_D5
    #define DASH_S3_PIN_LCD_D5 16
#endif
#ifndef DASH_S3_PIN_LCD_D6
    #define DASH_S3_PIN_LCD_D6 17
#endif
#ifndef DASH_S3_PIN_LCD_D7
    #define DASH_S3_PIN_LCD_D7 18
#endif
/** Write strobe. The i80 peripheral calls this PCLK. */
#ifndef DASH_S3_PIN_LCD_WR
    #define DASH_S3_PIN_LCD_WR 8
#endif
/** Data/Command select. Low for a command byte, high for parameters and
 * pixel data. Driven by the peripheral, not by GPIO writes. */
#ifndef DASH_S3_PIN_LCD_DC
    #define DASH_S3_PIN_LCD_DC 9
#endif
#ifndef DASH_S3_PIN_LCD_CS
    #define DASH_S3_PIN_LCD_CS 10
#endif
#ifndef DASH_S3_PIN_LCD_RST
    #define DASH_S3_PIN_LCD_RST 11
#endif
/** Backlight, driven by LEDC PWM so the brightness setting means
 * something. On the v3 PCB this is the TPS61169's CTRL input; on the
 * bench it is whatever drives the panel's LED string. */
#ifndef DASH_S3_PIN_LCD_BL
    #define DASH_S3_PIN_LCD_BL 12
#endif
/** Tearing-effect output from the ILI9488 (JP1 pin 22 on the breakout):
 * a pulse at the start of each vertical blank, which is when a frame
 * push starts so the write stays ahead of the panel's scan. An input;
 * set to -1 to run unsynchronised with nothing wired. */
#ifndef DASH_S3_PIN_LCD_TE
    #define DASH_S3_PIN_LCD_TE 47
#endif

/**
 * ----------------------------------------------------------------------
 * Touch -- FT6236 over I2C at 0x38. INT is not used (polled).
 *---------------------------------------------------------------------*/

#ifndef DASH_S3_PIN_TOUCH_SDA
    #define DASH_S3_PIN_TOUCH_SDA 13
#endif
#ifndef DASH_S3_PIN_TOUCH_SCL
    #define DASH_S3_PIN_TOUCH_SCL 14
#endif
#ifndef DASH_S3_PIN_TOUCH_RST
    #define DASH_S3_PIN_TOUCH_RST 21
#endif

/**
 * ----------------------------------------------------------------------
 * Resistive touch -- XPT2046 over SPI, the alternative to the FT6236
 * above; only one is wired at a time (dash_s3_platform.cpp picks). On the
 * ER-TFTM035-6 breakout this is JP1 pins 30 (RTP_/CS), 32 (SDO -> MISO),
 * 33 (SCL -> SCK), 34 (SDI -> MOSI). /RTP_PEN (pin 31) is unused.
 *
 * The DevKitC-1's J1 header is fully spoken for by the LCD, so SCK/MOSI
 * reuse the I2C pins on J1 and MISO/CS take the two adjacent spares at
 * the top of J3.
 *---------------------------------------------------------------------*/

#ifndef DASH_S3_PIN_RTP_SCK
    #define DASH_S3_PIN_RTP_SCK 14
#endif
#ifndef DASH_S3_PIN_RTP_MOSI
    #define DASH_S3_PIN_RTP_MOSI 13
#endif
#ifndef DASH_S3_PIN_RTP_MISO
    #define DASH_S3_PIN_RTP_MISO 1
#endif
#ifndef DASH_S3_PIN_RTP_CS
    #define DASH_S3_PIN_RTP_CS 2
#endif

/**
 * ----------------------------------------------------------------------
 * CAN -- the S3's TWAI controller, through an external 3.3V transceiver
 * (SN65HVD230 / TJA1051T/3). TX goes to the transceiver's TXD (D), RX
 * comes from its RXD (R).
 *---------------------------------------------------------------------*/

#ifndef DASH_S3_PIN_CAN_TX
    #define DASH_S3_PIN_CAN_TX 41
#endif
#ifndef DASH_S3_PIN_CAN_RX
    #define DASH_S3_PIN_CAN_RX 42
#endif
