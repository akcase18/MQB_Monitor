#include "relay_app.hpp"

#include <WiFi.h>

#include "remote_protocol.hpp"

namespace esp32
{
    using remote::protocol::MsgType;

    RelayApp::RelayApp(Stream &stream, RelayExtension *extension)
        : link_(stream),
          server_(80),
          web_(link_),
          extension_(extension)
    {
    }

    void RelayApp::begin()
    {
        /* Touching WiFi for the first time lazily inside on_frame() (on
         * the first real CMD_AP_ENABLE, which could arrive at any point
         * after boot) races the ESP-IDF lwIP task's own startup --
         * observed as a hard crash ("assert failed: tcpip_send_msg_wait_sem
         * ... Invalid mbox") the first time WiFi.softAP() ran. Doing the
         * mode transition once here instead gives that internal task the
         * rest of boot to settle before anything calls into WiFi again.
         * Stays disconnected (no AP broadcasting) until a real
         * CMD_AP_ENABLE. */
        WiFi.mode(WIFI_AP);
        WiFi.softAPdisconnect(true);

        web_.attach(server_);
        if (extension_ != nullptr)
        {
            extension_->attach(server_, link_);
        }
        server_.begin();
    }

    void RelayApp::poll(uint32_t now_ms)
    {
        link_.poll(on_frame, this, now_ms);
        server_.handleClient();
    }

    void RelayApp::on_frame(void          *ctx,
                            MsgType        type,
                            const uint8_t *payload,
                            uint16_t       len)
    {
        auto *self = static_cast<RelayApp *>(ctx);

        switch (type)
        {
            case MsgType::CMD_AP_ENABLE: {
                remote::protocol::ApEnableWire wire {};
                remote::protocol::decode_ap_enable(payload, wire);
                self->ap_.enable(wire.ssid, wire.password);

                remote::protocol::ApReadyWire ready {};
                self->ap_.get_ip(ready.ip);
                uint8_t  buf[sizeof(ready.ip)];
                uint8_t *end = remote::protocol::encode_ap_ready(buf, ready);
                self->link_.send_event(MsgType::EVT_AP_READY,
                                       buf,
                                       static_cast<uint16_t>(end - buf));
                break;
            }

            case MsgType::CMD_AP_DISABLE:
                self->ap_.disable();
                self->link_.send_event(MsgType::EVT_AP_DISABLED, nullptr, 0);
                break;

            default:
                if (self->extension_ != nullptr)
                {
                    self->extension_->on_frame(type, payload, len);
                }
                break;
        }
    }

} // namespace esp32
