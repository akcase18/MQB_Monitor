/**
 * @file teensy_serial_link.cpp
 * @brief ISerialLink over Serial4 — a spare hardware UART, wired to the
 * companion ESP32-C3. Pin routing is a hardware step (see the physical
 * redesign tracked in TODO.md); this just claims Serial4 by convention.
 */

#include "teensy_serial_link.hpp"

#include <Arduino.h>

TeensySerialLink &TeensySerialLink::instance()
{
    static TeensySerialLink s_instance;
    return s_instance;
}

void TeensySerialLink::begin(uint32_t baud)
{
    Serial4.begin(baud);
    Serial4.setTimeout(0);
}

size_t TeensySerialLink::read(uint8_t *buf, size_t len)
{
    /* readBytes() blocks up to Serial4's timeout waiting to fill `len` —
     * bounding the request to what's already buffered keeps this call
     * non-blocking regardless of timeout config, matching ISerialLink's
     * contract (poll()-driven, called once per loop() iteration). */
    size_t avail = static_cast<size_t>(Serial4.available());
    size_t n     = avail < len ? avail : len;
    return n > 0 ? static_cast<size_t>(Serial4.readBytes(buf, n)) : 0;
}

size_t TeensySerialLink::write(const uint8_t *buf, size_t len)
{
    return Serial4.write(buf, len);
}
