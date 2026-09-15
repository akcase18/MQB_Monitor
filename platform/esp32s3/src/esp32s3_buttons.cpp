#include "esp32s3_buttons.hpp"

#include "esp32s3_pins.hpp"

#include <Arduino.h>

namespace
{
    /* Indexed the way core::ButtonInput indexes them: 0 is A, 1 is B. */
    constexpr uint8_t PINS[] = { ESP32S3_PIN_BUTTON_A, ESP32S3_PIN_BUTTON_B };

    constexpr uint8_t POPULATED = ESP32S3_BUTTON_COUNT;

    static_assert(POPULATED <= (sizeof(PINS) / sizeof(PINS[0])),
                  "ESP32S3_BUTTON_COUNT names more buttons than there are "
                  "pins for");
} // namespace

Esp32s3Buttons &Esp32s3Buttons::instance()
{
    static Esp32s3Buttons s_instance;
    return s_instance;
}

void Esp32s3Buttons::begin()
{
    for (uint8_t i = 0; i < POPULATED; i++)
    {
        pinMode(PINS[i], INPUT_PULLUP);
    }
}

uint8_t Esp32s3Buttons::count() const
{
    return POPULATED;
}

bool Esp32s3Buttons::is_down(uint8_t idx) const
{
    if (idx >= POPULATED)
    {
        /**
         * Out of range reads as not-pressed, so code written for two
         * buttons behaves sanely on a one-button board -- see IButtons. */
        return false;
    }

    /* Active low: pulled up when released, shorted to ground when held. */
    return digitalRead(PINS[idx]) == LOW;
}
