/**
 * @file main.cpp
 * @brief ESP32-C3 relay firmware entry point: RelayApp (relay_app.hpp)
 * over the UART to the Teensy. Everything the relay does -- SoftAP
 * up/down for gui::WirelessSetupScreen's QR-code flow, the phone-facing
 * config form relaying REQ_GET_STATE/REQ_APPLY_CONFIG -- lives in
 * RelayApp; this file is only the transport. See platform/esp32/README.md
 * for how to test this against the desktop simulator with no Teensy or
 * display wiring at all.
 *
 * The relay link runs on Serial1 (UART) wired to the Teensy's Serial2,
 * leaving native USB-CDC free for flashing and debug logs. It used to run
 * on `Serial` (USB-CDC) so the firmware could be driven from a laptop with
 * no Teensy attached -- convenient for bring-up, but on a board where both
 * chips are soldered down it meant the two ends were on entirely different
 * transports: the Teensy sent CMD_AP_ENABLE on Serial2 and nothing was
 * listening, so WirelessSetupScreen span on its spinner forever waiting
 * for an EVT_AP_READY that could not arrive.
 */
#include <Arduino.h>

#include "relay_app.hpp"
#include "remote_protocol.hpp"

namespace
{
    /* Relay UART. Defaults are the XIAO ESP32-C3's Serial1 pins: GPIO21 is
     * TX (silkscreen D6), GPIO20 is RX (D7). Change these if the board
     * routes the link elsewhere.
     *
     * Crossover, since it is the easy thing to get backwards:
     *     Teensy TX (MicroMod pin 17) -> ESP32 RX (GPIO20)
     *     Teensy RX (MicroMod pin 16) <- ESP32 TX (GPIO21)
     *
     * The Teensy end is Serial2, not Serial4: MicroMod swaps those two
     * relative to a Teensy 4.0, so 16/17 is Serial2 (LPUART3) here. See
     * platform/teensy/src/teensy_serial_link.cpp. */
    constexpr int      RELAY_RX_PIN = 20;
    constexpr int      RELAY_TX_PIN = 21;
    constexpr uint32_t RELAY_BAUD   = 115200; /* must match
                                                 REMOTE_SERIAL_BAUD in
                                                 src/main.cpp */

    esp32::RelayApp *relay_app = nullptr;
} // namespace

void setup()
{
    /* USB-CDC is debug output only -- the relay is on Serial1. */
    Serial.begin(115200);

    /* The default RX ring is 256 bytes, but RESP_STATE's payload alone can
     * exceed 1200, and RelayLink only drains the port once per loop() pass
     * -- so a large frame overruns the buffer before it is read, which
     * showed up as send_request_and_wait() timing out on every real
     * REQ_GET_STATE round trip. Must be set before begin(), which would
     * otherwise install the 256 default. */
    Serial1.setRxBufferSize(remote::protocol::FRAME_MAX_PAYLOAD + 8);
    Serial1.begin(RELAY_BAUD, SERIAL_8N1, RELAY_RX_PIN, RELAY_TX_PIN);

    static esp32::RelayApp app(Serial1);
    relay_app = &app;
    app.begin();
}

void loop()
{
    relay_app->poll(millis());
}
