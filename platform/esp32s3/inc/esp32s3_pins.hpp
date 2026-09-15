/**
 * @file esp32s3_pins.hpp
 * @brief Every pin this board uses, in one place, and every one of them
 * overridable from platformio.ini.
 *
 * **The pinout is not frozen.** The build plan lists it as an open
 * decision, and the PCB does not exist yet. So none of these is written
 * into a driver: each is a `-D` an `[env:]` can set, and the defaults
 * below are a coherent starting set rather than a commitment. Changing one
 * is a line in `platformio.ini`, not an edit to a driver.
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
 * | 33-37 | Octal PSRAM bus on an R8 module (`N16R8` is the plan's part) |
 * | 43, 44 | UART0, the console this firmware logs to |
 *
 * That leaves 1-18 and 21 comfortably free on any S3 module, which is
 * where everything below sits.
 *
 * ## RPM
 *
 * GPIO15 is reserved and deliberately unused. The build plan's 9.1 argues
 * for designing an RPM input in now even if it is not populated: without
 * engine speed there is no honest low-oil-pressure threshold, and the
 * ESP32's PCNT peripheral counts pulses on any GPIO at no ADC cost.
 * Reserving the pin is nearly free; retrofitting it after the board is
 * laid out is not.
 */
#pragma once

/**
 * ----------------------------------------------------------------------
 * Display -- ST7789V2 over SPI. Write-only, so no MISO.
 *---------------------------------------------------------------------*/

#ifndef ESP32S3_PIN_LCD_SCLK
    #define ESP32S3_PIN_LCD_SCLK 12
#endif
#ifndef ESP32S3_PIN_LCD_MOSI
    #define ESP32S3_PIN_LCD_MOSI 11
#endif
#ifndef ESP32S3_PIN_LCD_CS
    #define ESP32S3_PIN_LCD_CS 10
#endif
/** Data/Command select. Low for a command byte, high for pixel data. */
#ifndef ESP32S3_PIN_LCD_DC
    #define ESP32S3_PIN_LCD_DC 9
#endif
#ifndef ESP32S3_PIN_LCD_RST
    #define ESP32S3_PIN_LCD_RST 8
#endif
/** Backlight, driven by LEDC PWM rather than a plain digital write, so
 * the brightness setting means something. */
#ifndef ESP32S3_PIN_LCD_BL
    #define ESP32S3_PIN_LCD_BL 21
#endif

/**
 * ----------------------------------------------------------------------
 * ADS1115 -- I2C. Two pins, which is the whole reason the ADC is not on
 * the SPI bus: the display keeps that to itself.
 *---------------------------------------------------------------------*/

#ifndef ESP32S3_PIN_I2C_SDA
    #define ESP32S3_PIN_I2C_SDA 4
#endif
#ifndef ESP32S3_PIN_I2C_SCL
    #define ESP32S3_PIN_I2C_SCL 5
#endif

/**
 * ----------------------------------------------------------------------
 * Buttons -- active-low against the internal pull-ups.
 *
 * Both footprints are routed and populated even if only one switch is
 * fitted: an unpopulated footprint is free, and adding one after layout
 * is not. core::ActionMap::set_defaults() re-shapes itself for a
 * one-button board, so this is a BOM decision rather than a firmware one.
 *---------------------------------------------------------------------*/

#ifndef ESP32S3_PIN_BUTTON_A
    #define ESP32S3_PIN_BUTTON_A 6
#endif
#ifndef ESP32S3_PIN_BUTTON_B
    #define ESP32S3_PIN_BUTTON_B 7
#endif

/** Set to 1 to populate only button A -- see core/input's README for
 * which actions a single-button map cannot reach. */
#ifndef ESP32S3_BUTTON_COUNT
    #define ESP32S3_BUTTON_COUNT 2
#endif

/**
 * ----------------------------------------------------------------------
 * Reserved, not driven by this firmware.
 *---------------------------------------------------------------------*/

/** Engine speed, for PCNT. See the file comment. */
#ifndef ESP32S3_PIN_RPM_RESERVED
    #define ESP32S3_PIN_RPM_RESERVED 15
#endif
