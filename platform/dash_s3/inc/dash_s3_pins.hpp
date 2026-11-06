/**
 * @file dash_s3_pins.hpp
 * @brief Every pin this board uses, in one place, and every one of them
 * overridable from platformio.ini.
 *
 * The defaults below are the dash PCB's pinout (ESP32-S3-WROOM-1 N16R2).
 * Each is a `-D` an `[env:]` can override, which is how
 * env:dash_s3_devkit puts back the old N16R8 DevKitC-1 bench wiring.
 * Changing one is a line in `platformio.ini`.
 *
 * ## Pins to avoid
 *
 * - 0, 3, 45, 46: Strapping pins.
 * - 19, 20:       Dedicated USB D-/D+
 * - 26, 32:       SPI flash bus
 */
#pragma once

/**
 * ----------------------------------------------------------------------
 * Display -- ILI9488 over an 8-bit 8080 parallel bus (LCD_CAM i80).
 * Write-only.
 * ---------------------------------------------------------------------
 */

#ifndef DASH_S3_PIN_LCD_D0
    #define DASH_S3_PIN_LCD_D0 42
#endif
#ifndef DASH_S3_PIN_LCD_D1
    #define DASH_S3_PIN_LCD_D1 41
#endif
#ifndef DASH_S3_PIN_LCD_D2
    #define DASH_S3_PIN_LCD_D2 40
#endif
#ifndef DASH_S3_PIN_LCD_D3
    #define DASH_S3_PIN_LCD_D3 39
#endif
#ifndef DASH_S3_PIN_LCD_D4
    #define DASH_S3_PIN_LCD_D4 38
#endif
#ifndef DASH_S3_PIN_LCD_D5
    #define DASH_S3_PIN_LCD_D5 37
#endif
#ifndef DASH_S3_PIN_LCD_D6
    #define DASH_S3_PIN_LCD_D6 36
#endif
#ifndef DASH_S3_PIN_LCD_D7
    #define DASH_S3_PIN_LCD_D7 35
#endif
#ifndef DASH_S3_PIN_LCD_WR
    #define DASH_S3_PIN_LCD_WR 21
#endif
/**
 * Data/Command select. Low for a command byte, high for parameters and
 * pixel data. Driven by the peripheral, not by GPIO writes.
 */
#ifndef DASH_S3_PIN_LCD_DC
    #define DASH_S3_PIN_LCD_DC 14
#endif
#ifndef DASH_S3_PIN_LCD_CS
    #define DASH_S3_PIN_LCD_CS 13
#endif
#ifndef DASH_S3_PIN_LCD_RST
    #define DASH_S3_PIN_LCD_RST 8
#endif
/**
 * Backlight, driven by LEDC PWM so the brightness setting means
 * something. On the PCB this is BL_CTRL, the PAM2804's EN input (500Hz
 * PWM, see dash_s3_display.cpp).
 */
#ifndef DASH_S3_PIN_LCD_BL
    #define DASH_S3_PIN_LCD_BL 10
#endif
/**
 * Tearing-effect output from the ILI9488 (JP1 pin 22 on the breakout):
 * a pulse at the start of each vertical blank, which is when a frame
 * push starts so the write stays ahead of the panel's scan. An input;
 * -1 runs unsynchronised, which is the PCB (TE is not routed).
 */
#ifndef DASH_S3_PIN_LCD_TE
    #define DASH_S3_PIN_LCD_TE -1
#endif

/**
 * ----------------------------------------------------------------------
 * Capacitive touch: FT6236 over I2C at 0x38. INT (TOUCH_PEN_INT, IO12 on
 * the PCB) is not used (polled).
 * ---------------------------------------------------------------------
 */

#ifndef DASH_S3_PIN_TOUCH_SDA
    #define DASH_S3_PIN_TOUCH_SDA 1
#endif
#ifndef DASH_S3_PIN_TOUCH_SCL
    #define DASH_S3_PIN_TOUCH_SCL 2
#endif
#ifndef DASH_S3_PIN_TOUCH_RST
    #define DASH_S3_PIN_TOUCH_RST 11
#endif

/**
 * ----------------------------------------------------------------------
 * Resistive touch: XPT2046 over SPI, the alternative to the FT6236
 * above; only one is wired at a time (dash_s3_platform.cpp picks). On the
 * ER-TFTM035-6 breakout this is JP1 pins 30 (RTP_/CS), 32 (SDO -> MISO),
 * 33 (SCL -> SCK), 34 (SDI -> MOSI). /RTP_PEN (pin 31) is unused.
 *
 * Bench-only: the PCB has no XPT2046. These defaults are the DevKitC-1
 * wiring (they overlap the PCB's LCD pins, which doesn't matter since
 * only one board ever builds with DASH_S3_TOUCH_USE_RESISTIVE).
 * ---------------------------------------------------------------------
 */

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
 * CAN: the S3's TWAI controller, through an external 3.3V transceiver
 * (SN65HVD230 / TJA1051T/3). TX goes to the transceiver's TXD (D), RX
 * comes from its RXD (R).
 * ---------------------------------------------------------------------
 */

#ifndef DASH_S3_PIN_CAN_TX
    #define DASH_S3_PIN_CAN_TX 47
#endif
#ifndef DASH_S3_PIN_CAN_RX
    #define DASH_S3_PIN_CAN_RX 48
#endif
