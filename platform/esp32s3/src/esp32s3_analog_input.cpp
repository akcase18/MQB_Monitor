#include "esp32s3_analog_input.hpp"

#include "esp32s3_pins.hpp"

#include <Arduino.h>
#include <Wire.h>

namespace
{
    constexpr uint8_t REG_CONVERSION = 0x00;
    constexpr uint8_t REG_CONFIG     = 0x01;

    /**
     * Config register fields, as they are assembled in start_conversion()
     * below. Named rather than written as one hex literal, because a
     * wrong PGA is a reading that is off by a factor of two and otherwise
     * looks completely reasonable. */
    constexpr uint16_t CFG_OS_SINGLE = 0x8000; /* start a conversion */
    constexpr uint16_t CFG_MUX_SINGLE_ENDED_BASE = 0x4000; /* AIN0 vs GND */
    constexpr uint16_t CFG_MUX_SHIFT             = 12;
    constexpr uint16_t CFG_PGA_4V096             = 0x0200;
    constexpr uint16_t CFG_MODE_SINGLE_SHOT      = 0x0100;
    constexpr uint16_t CFG_DR_860SPS             = 0x00E0;
    constexpr uint16_t CFG_COMP_DISABLE          = 0x0003;

    /** Microvolts per LSB at the +/-4.096V range: 4.096V / 32768. */
    constexpr uint32_t MICROVOLTS_PER_LSB = 125;

    /**
     * A conversion at 860SPS is ~1.2ms. Ten is a generous ceiling that
     * still bounds the loop: a held bus or an absent part must not stall
     * the dashboard forever, it must fail the read so the plausibility
     * layer blanks the panel. */
    constexpr uint32_t CONVERSION_TIMEOUT_MS = 10;

    constexpr uint32_t I2C_HZ = 400000;
} // namespace

Esp32s3AnalogInput &Esp32s3AnalogInput::instance()
{
    static Esp32s3AnalogInput s_instance;
    return s_instance;
}

void Esp32s3AnalogInput::begin()
{
    if (began_)
    {
        return;
    }
    began_ = true;

    Wire.begin(ESP32S3_PIN_I2C_SDA, ESP32S3_PIN_I2C_SCL, I2C_HZ);
}

uint8_t Esp32s3AnalogInput::channel_count() const
{
    return CHANNEL_COUNT;
}

bool Esp32s3AnalogInput::write_register(uint8_t reg, uint16_t value)
{
    Wire.beginTransmission(I2C_ADDRESS);
    Wire.write(reg);
    Wire.write(static_cast<uint8_t>(value >> 8));
    Wire.write(static_cast<uint8_t>(value & 0xFF));
    return Wire.endTransmission() == 0;
}

bool Esp32s3AnalogInput::read_register(uint8_t reg, uint16_t &out)
{
    Wire.beginTransmission(I2C_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission() != 0)
    {
        return false;
    }

    if (Wire.requestFrom(static_cast<int>(I2C_ADDRESS), 2) != 2)
    {
        return false;
    }

    uint16_t hi = static_cast<uint16_t>(Wire.read());
    uint16_t lo = static_cast<uint16_t>(Wire.read());
    out         = static_cast<uint16_t>((hi << 8) | lo);
    return true;
}

bool Esp32s3AnalogInput::read_microvolts(uint8_t ch, uint32_t &out)
{
    if (!began_ || ch >= CHANNEL_COUNT)
    {
        return false;
    }

    uint16_t config = CFG_OS_SINGLE
                      | static_cast<uint16_t>(
                          CFG_MUX_SINGLE_ENDED_BASE
                          | (static_cast<uint16_t>(ch) << CFG_MUX_SHIFT))
                      | CFG_PGA_4V096 | CFG_MODE_SINGLE_SHOT | CFG_DR_860SPS
                      | CFG_COMP_DISABLE;

    if (!write_register(REG_CONFIG, config))
    {
        return false;
    }

    /**
     * Poll the OS bit rather than delaying a fixed 1.2ms: the part tells
     * us when it is done, and a fixed delay would be either wrong at a
     * different data rate or slower than necessary at this one. */
    uint32_t started = millis();
    for (;;)
    {
        uint16_t status = 0;
        if (!read_register(REG_CONFIG, status))
        {
            return false;
        }
        if ((status & CFG_OS_SINGLE) != 0)
        {
            break; /* OS reads back 1 when the device is idle. */
        }
        if ((millis() - started) > CONVERSION_TIMEOUT_MS)
        {
            /**
             * A false here means "the value is unknown", which is not the
             * same as a valid reading of 0V and must never be treated as
             * one -- see IAnalogInput. */
            return false;
        }
    }

    uint16_t raw = 0;
    if (!read_register(REG_CONVERSION, raw))
    {
        return false;
    }

    /**
     * The conversion register is signed. A single-ended input should never
     * be negative, but noise around zero genuinely goes a count or two
     * below it, and that is a real reading of approximately zero volts
     * rather than an error -- so it clamps rather than failing. */
    int16_t signed_raw = static_cast<int16_t>(raw);
    if (signed_raw < 0)
    {
        signed_raw = 0;
    }

    out = static_cast<uint32_t>(signed_raw)
          * MICROVOLTS_PER_LSB;
    return true;
}
