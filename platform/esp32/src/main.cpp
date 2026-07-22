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
 * Serial is native USB-CDC on this board (no separate UART bridge chip) —
 * the same physical connection used to flash it doubles as the relay link
 * for now. Once wired to a real Teensy, swap `Serial` below for `Serial1`
 * on two GPIOs so native USB is free for flashing/debug logs only.
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
    /* Arduino-ESP32's native USB-CDC defaults to a 256-byte RX ring
     * buffer (USBCDC::begin() -> setRxBufferSize(256) if not preset) --
     * fine for the tiny CMD_AP_ENABLE/EVT_AP_* frames, but RESP_STATE's
     * payload alone can be over 1200 bytes, and a real USB transfer
     * arrives in fast bursts (not paced like a UART), so it blows past
     * that buffer well before RelayLink's main-loop polling code gets a
     * chance to drain it -- observed as send_request_and_wait() timing
     * out on every real REQ_GET_STATE round trip. Must be set before
     * begin() so it isn't immediately overwritten by that 256 default.
     */
    Serial.setRxBufferSize(remote::protocol::FRAME_MAX_PAYLOAD + 8);
    Serial.begin(115200);

    /* Touching WiFi for the first time lazily inside on_relay_frame() (on
     * the first real CMD_AP_ENABLE, which could arrive at any point after
     * boot) races the ESP-IDF lwIP task's own startup — observed as a hard
     * crash ("assert failed: tcpip_send_msg_wait_sem ... Invalid mbox")
     * the first time WiFi.softAP() ran. Doing the mode transition once
     * here instead gives that internal task the whole rest of setup()/
     * boot to settle before anything ever calls into WiFi again. Stays
     * disconnected (no AP broadcasting) until a real CMD_AP_ENABLE. */
    WiFi.mode(WIFI_AP);
    WiFi.softAPdisconnect(true);

    static esp32::RelayLink link(Serial);
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
