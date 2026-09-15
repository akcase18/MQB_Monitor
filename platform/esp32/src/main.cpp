/**
 * @file main.cpp
 * @brief ESP32-C3 relay firmware entry point. Reacts to CMD_AP_ENABLE/
 * CMD_AP_DISABLE frames from the Teensy by driving the SoftAP up/down and
 * replying EVT_AP_READY/EVT_AP_DISABLED — the half of remote::protocol
 * gui::WirelessSetupScreen's QR-code flow needs — and serves the
 * phone-facing wireless config form (ConfigWebHandler, config_web.hpp),
 * which relays REQ_GET_STATE/REQ_APPLY_CONFIG to the Teensy over the same
 * link. See platform/esp32/README.md for how to test this against the
 * desktop simulator with no Teensy or display wiring at all.
 *
 * The relay link runs on Serial1 (UART) wired to the Teensy's Serial4,
 * leaving native USB-CDC free for flashing and debug logs. It used to run
 * on `Serial` (USB-CDC) so the firmware could be driven from a laptop with
 * no Teensy attached — convenient for bring-up, but on a board where both
 * chips are soldered down it meant the two ends were on entirely different
 * transports: the Teensy sent CMD_AP_ENABLE on Serial4 and nothing was
 * listening, so WirelessSetupScreen span on its spinner forever waiting
 * for an EVT_AP_READY that could not arrive.
 *
 * See platform/esp32/README.md for driving this from the desktop simulator,
 * which now needs a USB-serial adapter on these pins rather than the
 * flashing cable.
 */
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "ap_manager.hpp"
#include "config_web.hpp"
#include "relay_link.hpp"
#include "remote_protocol.hpp"

namespace
{
    /**
     * Relay UART. Defaults are the XIAO ESP32-C3's Serial1 pins: GPIO21 is
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

    esp32::ApManager  ap_manager;
    esp32::RelayLink *relay_link = nullptr;
    WebServer         server(80);

    void on_relay_frame(void                     *ctx,
                        remote::protocol::MsgType type,
                        const uint8_t            *payload,
                        uint16_t                  len)
    {
        (void)ctx;
        (void)len;

        switch (type)
        {
            case remote::protocol::MsgType::CMD_AP_ENABLE: {
                remote::protocol::ApEnableWire wire {};
                remote::protocol::decode_ap_enable(payload, wire);
                ap_manager.enable(wire.ssid, wire.password);

                remote::protocol::ApReadyWire ready {};
                ap_manager.get_ip(ready.ip);
                uint8_t  buf[sizeof(ready.ip)];
                uint8_t *end = remote::protocol::encode_ap_ready(buf, ready);
                relay_link->send_event(remote::protocol::MsgType::EVT_AP_READY,
                                       buf,
                                       static_cast<uint16_t>(end - buf));
                break;
            }

            case remote::protocol::MsgType::CMD_AP_DISABLE:
                ap_manager.disable();
                relay_link->send_event(
                    remote::protocol::MsgType::EVT_AP_DISABLED, nullptr, 0);
                break;

            default:
                break;
        }
    }
} // namespace

void setup()
{
    /**
     * Arduino-ESP32's native USB-CDC defaults to a 256-byte RX ring
     * buffer (USBCDC::begin() -> setRxBufferSize(256) if not preset) --
     * fine for the tiny CMD_AP_ENABLE/EVT_AP_* frames, but RESP_STATE's
     * payload alone can be over 1200 bytes, and a real USB transfer
     * arrives in fast bursts (not paced like a UART), so it blows past
     * that buffer well before RelayLink's main-loop polling code gets a
     * chance to drain it -- observed as send_request_and_wait() timing
     * out on every real REQ_GET_STATE round trip. Must be set before
     * begin() so it isn't immediately overwritten by that 256 default.
     */
    /* USB-CDC is debug output only now -- the relay moved to Serial1. */
    Serial.begin(115200);

    /**
     * The default RX ring is 256 bytes, but RESP_STATE's payload alone can
     * exceed 1200, and RelayLink only drains the port once per loop() pass
     * -- so a large frame overruns the buffer before it is read, which
     * showed up as send_request_and_wait() timing out on every real
     * REQ_GET_STATE round trip. Must be set before begin(), which would
     * otherwise install the 256 default. */
    Serial1.setRxBufferSize(remote::protocol::FRAME_MAX_PAYLOAD + 8);
    Serial1.begin(RELAY_BAUD, SERIAL_8N1, RELAY_RX_PIN, RELAY_TX_PIN);

    /**
     * Touching WiFi for the first time lazily inside on_relay_frame() (on
     * the first real CMD_AP_ENABLE, which could arrive at any point after
     * boot) races the ESP-IDF lwIP task's own startup — observed as a hard
     * crash ("assert failed: tcpip_send_msg_wait_sem ... Invalid mbox")
     * the first time WiFi.softAP() ran. Doing the mode transition once
     * here instead gives that internal task the whole rest of setup()/
     * boot to settle before anything ever calls into WiFi again. Stays
     * disconnected (no AP broadcasting) until a real CMD_AP_ENABLE. */
    WiFi.mode(WIFI_AP);
    WiFi.softAPdisconnect(true);

    static esp32::RelayLink link(Serial1);
    relay_link = &link;

    static esp32::ConfigWebHandler config_web(link);
    config_web.attach(server);

    server.begin();
}

void loop()
{
    relay_link->poll(on_relay_frame, nullptr, millis());
    server.handleClient();
}
