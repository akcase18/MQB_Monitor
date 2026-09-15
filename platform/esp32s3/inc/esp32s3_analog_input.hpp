/**
 * @file esp32s3_analog_input.hpp
 * @brief IAnalogInput over an ADS1115 on I2C.
 *
 * Four single-ended 16-bit channels, internal reference, programmable
 * gain, two pins. The SPI bus stays entirely for the display.
 *
 * ## Not the ESP32-S3's own ADC, on purpose
 *
 * ADC2 is unusable whenever Wi-Fi is active, and on-board Wi-Fi is this
 * product's entire premise. The internal SAR is also noisy and non-linear
 * near both rails, and Wi-Fi transmit bursts couple into the supply -- so
 * the error would correlate with radio activity, which is the worst
 * possible shape for a fault nobody is looking for.
 *
 * ## Sizing
 *
 * At the +/-4.096V range one LSB is 125uV. Through the board's 0.6x
 * divider that is about 208uV referred to the sensor, which is why
 * IAnalogInput talks in microvolts and not millivolts -- millivolts would
 * throw away most of the part.
 *
 * ## It blocks
 *
 * A single-shot conversion at 860SPS takes about 1.2ms and this waits for
 * it. That is deliberate rather than overlooked: core::AnalogSignalSource
 * samples one channel per slot, so a loop pass spends ~1.2ms here and the
 * rest on LVGL. Sweeping all four channels per call would cost 5ms and
 * drop a frame. If that ever stops being acceptable, the fix is to start
 * a conversion on one pass and collect it on the next -- the round-robin
 * structure above already suits it.
 *
 * The `ADDR` pin gives four addresses, so a second ADS1115 adds four more
 * channels with no new pins; `CHANNEL_COUNT` is what to raise then.
 */
#pragma once

#include "platform.hpp"

class Esp32s3AnalogInput : public IAnalogInput
{
    public:
        /** One ADS1115. ADDR tied low. */
        static constexpr uint8_t I2C_ADDRESS = 0x48;

        static constexpr uint8_t CHANNEL_COUNT = 4;

        static Esp32s3AnalogInput &instance();

        void    begin() override;
        uint8_t channel_count() const override;
        bool    read_microvolts(uint8_t ch, uint32_t &out) override;

    private:
        Esp32s3AnalogInput() = default;

        bool write_register(uint8_t reg, uint16_t value);
        bool read_register(uint8_t reg, uint16_t &out);

        bool began_ = false;
};
