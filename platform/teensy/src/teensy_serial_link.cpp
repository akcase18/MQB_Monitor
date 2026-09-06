/**
 * @file teensy_serial_link.cpp
 * @brief ISerialLink over Serial2 — the hardware UART wired to the
 * companion ESP32-C3 on the v3 board, at MicroMod pins 16 (RX) / 17 (TX).
 *
 * MicroMod SWAPS Serial2 and Serial4 relative to a Teensy 4.0, and the
 * Teensyduino core does it silently on ARDUINO_TEENSY_MICROMOD
 * (cores/teensy4/HardwareSerial2.cpp and HardwareSerial4.cpp):
 *
 *              Teensy 4.0      Teensy MicroMod
 *   Serial2    pins 7/8        pins 16/17   (LPUART3)  <- this board
 *   Serial4    pins 16/17      pins 7/8     (LPUART4)
 *
 * So a 4.0 pinout diagram says "16/17 is Serial4", which compiles fine
 * here and quietly drives pins 7/8 instead. This file used to say Serial4
 * for exactly that reason -- the Teensy transmitted into unconnected pins
 * and WirelessSetupScreen span forever waiting for a reply. Check the pin
 * numbers against the MicroMod column, not the 4.0 one, if this ever moves.
 */

#include "teensy_serial_link.hpp"

#include <Arduino.h>

#include "remote_protocol.hpp"

namespace
{
    /* Teensyduino gives Serial2 a 64-byte RX ring and a 40-byte TX ring
     * (SERIAL2_RX_BUFFER_SIZE / SERIAL2_TX_BUFFER_SIZE in
     * cores/teensy4/HardwareSerial2.cpp). Both are far too small here.
     *
     * RX: RemoteConfigLink::poll() parses incrementally and drains until
     * the port is empty, so a frame need not fit all at once -- but the
     * ring still has to survive the gap BETWEEN polls, and poll() runs once
     * per loop() pass alongside lv_timer_handler(). At 115200 baud 64 bytes
     * is 5.5ms, while a full LVGL repaint is ten flushes of ~1.5ms DMA plus
     * render time. So any inbound frame bigger than 64 bytes outruns the
     * drain and is shredded mid-flight: the CRC fails and the frame is
     * dropped with no error anywhere. REQ_APPLY_CONFIG is the only large
     * inbound frame, which is why WiFi and page loads worked while applying
     * config silently did nothing.
     *
     * TX: RESP_STATE alone can exceed 1200 bytes. write() blocks until the
     * ring has room, so a 40-byte ring stalls loop() for most of the ~104ms
     * that transfer takes -- freezing CAN polling, UDS and the UI, and not
     * draining RX either.
     *
     * Sized to a whole max frame plus framing overhead so neither direction
     * depends on how long a loop() pass happens to take. */
    constexpr size_t LINK_BUF_SIZE = remote::protocol::FRAME_MAX_PAYLOAD + 64;

    uint8_t s_rx_buf[LINK_BUF_SIZE];
    uint8_t s_tx_buf[LINK_BUF_SIZE];
} // namespace

TeensySerialLink &TeensySerialLink::instance()
{
    static TeensySerialLink s_instance;
    return s_instance;
}

void TeensySerialLink::begin(uint32_t baud)
{
    Serial2.begin(baud);
    Serial2.setTimeout(0);

    /* After begin() -- it installs the small default rings, so extending
     * them first would just be overwritten. */
    Serial2.addMemoryForRead(s_rx_buf, sizeof(s_rx_buf));
    Serial2.addMemoryForWrite(s_tx_buf, sizeof(s_tx_buf));
}

size_t TeensySerialLink::read(uint8_t *buf, size_t len)
{
    /* readBytes() blocks up to Serial2's timeout waiting to fill `len` —
     * bounding the request to what's already buffered keeps this call
     * non-blocking regardless of timeout config, matching ISerialLink's
     * contract (poll()-driven, called once per loop() iteration). */
    size_t avail = static_cast<size_t>(Serial2.available());
    size_t n     = avail < len ? avail : len;
    return n > 0 ? static_cast<size_t>(Serial2.readBytes(buf, n)) : 0;
}

size_t TeensySerialLink::write(const uint8_t *buf, size_t len)
{
    return Serial2.write(buf, len);
}
