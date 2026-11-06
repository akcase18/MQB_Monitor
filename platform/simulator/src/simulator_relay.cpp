/**
 * @file simulator_relay.cpp
 * @brief See simulator_relay.hpp. Frames from remote/server go through
 * the same FrameReader the relay uses; replies queue up in to_dash_ for
 * the next read(), which is also where the fake upload's timeline runs,
 * since main.cpp polls it every loop.
 */

#include "simulator_relay.hpp"

#include <stdio.h>
#include <string.h>

#include "simulator_clock.hpp"

using remote::protocol::MsgType;
using remote::protocol::OtaResult;
namespace protocol = remote::protocol;

namespace
{
    /**
     * Long enough that the spinner registers before the QR code.
     */
    constexpr uint32_t AP_START_MS = 400;

    /**
     * A plausible image: about what env:dash_s3 builds to.
     */
    constexpr uint32_t    UPLOAD_BYTES   = 1235121;
    constexpr uint32_t    UPLOAD_MS      = 6000;
    constexpr uint32_t    PROGRESS_STEP  = 64u * 1024u; /* as DashS3Updater */
    constexpr const char *UPLOAD_VERSION = "v9.9.9-sim";

    /**
     * Long enough to see "Restarting..." before the window does.
     */
    constexpr uint32_t INSTALL_MS = 1500;

    /**
     * 192.168.4.1, the S3 SoftAP's address.
     */
    constexpr uint8_t AP_IP[4] = { 192, 168, 4, 1 };

    /**
     * As index.html's KEEPALIVE_MS: well inside the dash's 60 s session
     * timeout.
     */
    constexpr uint32_t PAGE_KEEPALIVE_MS = 20000;
} // namespace

SimulatorRelay &SimulatorRelay::instance()
{
    static SimulatorRelay s_instance;
    return s_instance;
}

void SimulatorRelay::begin(uint32_t baud)
{
    (void)baud;
    printf("[sim] Wi-Fi keys (Wi-Fi screen open): P = phone joins/leaves, "
           "V = phone opens the config page, H = page closed, D = Discard, "
           "U = phone asks for update mode, S = send firmware, F = send one "
           "that fails\n");
}

size_t SimulatorRelay::read(uint8_t *buf, size_t len)
{
    uint32_t now = SimulatorClock::instance().millis();

    if (ap_ready_at_ != 0 && now >= ap_ready_at_)
    {
        ap_ready_at_ = 0;
        ap_up_       = true;
        protocol::ApReadyWire ready {};
        memcpy(ready.ip, AP_IP, sizeof(ready.ip));
        uint8_t payload[sizeof(ready.ip)];
        protocol::encode_ap_ready(payload, ready);
        send(MsgType::EVT_AP_READY, payload, sizeof(payload));
    }

    if (uploading_)
    {
        run_upload(now);
    }

    if (page_open_ && !preview_.empty()
        && now - last_preview_ms_ >= PAGE_KEEPALIVE_MS)
    {
        last_preview_ms_ = now;
        send(MsgType::REQ_PREVIEW_CONFIG,
             preview_.data(),
             static_cast<uint16_t>(preview_.size()));
    }

    if (restart_at_ != 0 && now >= restart_at_)
    {
        printf("[sim] installing %s -- restarting\n", UPLOAD_VERSION);
        platform_restart();
    }

    size_t n = to_dash_.size() < len ? to_dash_.size() : len;
    memcpy(buf, to_dash_.data(), n);
    to_dash_.erase(to_dash_.begin(), to_dash_.begin() + n);
    return n;
}

size_t SimulatorRelay::write(const uint8_t *buf, size_t len)
{
    uint32_t now = SimulatorClock::instance().millis();
    for (size_t i = 0; i < len; i++)
    {
        if (reader_.on_byte(buf[i], now))
        {
            on_frame(reader_.decoded_frame(), now);
        }
    }
    return len;
}

void SimulatorRelay::on_key(char key)
{
    uint32_t now = SimulatorClock::instance().millis();
    if (strchr("pvhdu", key) != nullptr && !ap_up_)
    {
        printf("[sim] %c: open Settings -> Wi-Fi first -- the phone "
               "reaches the dash through the AP\n",
               key - 'a' + 'A');
        return;
    }

    switch (key)
    {
        case 'p': {
            phone_joined_ = !phone_joined_;
            uint8_t count = phone_joined_ ? 1 : 0;
            send(MsgType::EVT_AP_STATIONS, &count, 1);
            printf("[sim] phone %s\n", phone_joined_ ? "joined" : "left");
            if (!phone_joined_)
            {
                page_open_ = false;
            }
            break;
        }

        case 'v':
            join_phone();
            open_page();
            break;

        case 'h': {
            if (!page_open_)
            {
                printf("[sim] H: no page open -- press V first\n");
                return;
            }
            page_open_   = false;
            uint8_t left = protocol::REVERT_PAGE_LEFT;
            send(MsgType::REQ_REVERT_PREVIEW, &left, 1);
            printf("[sim] page closed\n");
            break;
        }

        case 'd':
            if (!page_open_ || preview_.empty())
            {
                printf("[sim] D: no page open -- press V first\n");
                return;
            }
            send(MsgType::REQ_REVERT_PREVIEW);
            last_preview_ms_ = now;
            send(MsgType::REQ_PREVIEW_CONFIG,
                 preview_.data(),
                 static_cast<uint16_t>(preview_.size()));
            printf("[sim] Discard\n");
            break;

        case 'u':
            join_phone();
            send(MsgType::EVT_OTA_REQUEST);
            break;

        case 's':
        case 'f':
            if (!armed_)
            {
                printf("[sim] %c: press U and tap Start first -- uploads "
                       "are refused outside update mode\n",
                       key - 'a' + 'A');
                return;
            }
            start_upload(key == 'f', now);
            break;

        default:
            break;
    }
}

void SimulatorRelay::on_frame(const protocol::DecodedFrame &frame,
                              uint32_t                      now)
{
    switch (frame.type)
    {
        case MsgType::CMD_AP_ENABLE:
            ap_ready_at_  = now + AP_START_MS;
            phone_joined_ = false;
            page_open_    = false;
            break;

        case MsgType::CMD_AP_DISABLE:
            ap_up_        = false;
            phone_joined_ = false;
            page_open_    = false;
            ap_ready_at_  = 0;
            send(MsgType::EVT_AP_DISABLED);
            break;

        case MsgType::CMD_OTA_ARM:
            armed_ = true;
            break;

        case MsgType::CMD_OTA_DISARM:
            armed_     = false;
            uploading_ = false;
            ready_     = false;
            break;

        case MsgType::CMD_OTA_COMMIT:
            if (!armed_ || !ready_)
            {
                uint8_t result = static_cast<uint8_t>(OtaResult::ErrFlash);
                send(MsgType::EVT_OTA_RESULT, &result, 1);
                break;
            }
            restart_at_ = now + INSTALL_MS;
            break;

        case MsgType::RESP_STATE:
            if (awaiting_state_)
            {
                awaiting_state_ = false;
                preview_from_state(frame, now);
            }
            break;

        default:
            /**
             * RESP_APPLY_RESULT: the fake page doesn't check.
             */
            break;
    }
}

void SimulatorRelay::send(MsgType type, const uint8_t *payload, uint16_t len)
{
    uint8_t frame[protocol::FRAME_MAX_PAYLOAD + 8];
    size_t  n = protocol::encode_frame(type, payload, len, frame, sizeof(frame));
    to_dash_.insert(to_dash_.end(), frame, frame + n);
}

void SimulatorRelay::start_upload(bool fail, uint32_t now)
{
    uploading_    = true;
    upload_fails_ = fail;
    upload_start_ = now;
    reported_     = 0;
    ready_        = false;

    protocol::OtaBeginWire begin {};
    begin.total_bytes = UPLOAD_BYTES;
    strncpy(begin.version, UPLOAD_VERSION, sizeof(begin.version) - 1);
    uint8_t payload[protocol::OTA_BEGIN_BYTES];
    protocol::encode_ota_begin(payload, begin);
    send(MsgType::EVT_OTA_BEGIN, payload, sizeof(payload));
}

void SimulatorRelay::run_upload(uint32_t now)
{
    uint32_t elapsed = now - upload_start_;
    uint32_t written
        = elapsed >= UPLOAD_MS
              ? UPLOAD_BYTES
              : static_cast<uint32_t>(static_cast<uint64_t>(UPLOAD_BYTES)
                                      * elapsed / UPLOAD_MS);

    if (upload_fails_ && written >= UPLOAD_BYTES / 2)
    {
        uploading_     = false;
        uint8_t result = static_cast<uint8_t>(OtaResult::ErrInterrupted);
        send(MsgType::EVT_OTA_RESULT, &result, 1);
        return;
    }

    if (written - reported_ >= PROGRESS_STEP && written < UPLOAD_BYTES)
    {
        reported_ = written;
        uint8_t payload[4];
        protocol::put_u32(payload, written);
        send(MsgType::EVT_OTA_PROGRESS, payload, sizeof(payload));
    }

    if (written >= UPLOAD_BYTES)
    {
        uploading_     = false;
        ready_         = true;
        uint8_t result = static_cast<uint8_t>(OtaResult::Ok);
        send(MsgType::EVT_OTA_RESULT, &result, 1);
    }
}

/**
 * Only a joined phone can open a page or ask for an update.
 */
void SimulatorRelay::join_phone()
{
    if (!phone_joined_)
    {
        phone_joined_ = true;
        uint8_t count = 1;
        send(MsgType::EVT_AP_STATIONS, &count, 1);
    }
}

/**
 * As the page's load(): read the state, and preview it once it's back.
 */
void SimulatorRelay::open_page()
{
    page_open_      = true;
    awaiting_state_ = true;
    send(MsgType::REQ_GET_STATE);
    printf("[sim] page opened\n");
}

/**
 * RESP_STATE is the settings, the panels, the catalog, then each
 * signal's config. A preview is the same minus the catalog, plus a value
 * per signal -- here part-way up each one's display range, so bars and
 * gauges show something.
 */
void SimulatorRelay::preview_from_state(const protocol::DecodedFrame &state,
                                        uint32_t                      now)
{
    const uint8_t *in = state.payload;

    protocol::SettingsWire settings {};
    in = protocol::decode_settings(in, settings);

    uint8_t panel_count = 0;
    in                  = protocol::get_u8(in, panel_count);
    std::vector<protocol::PanelWire> panels(panel_count);
    for (auto &panel : panels)
    {
        in = protocol::decode_panel(in, panel);
    }

    uint16_t signal_count = 0;
    in                    = protocol::get_u16(in, signal_count);
    std::vector<float> values(signal_count);
    for (uint16_t i = 0; i < signal_count; i++)
    {
        protocol::SignalCatalogEntryWire entry {};
        in = protocol::decode_signal_catalog_entry(in, entry);
        values[i] = entry.display_min
                    + (entry.display_max - entry.display_min)
                          * (0.3f + 0.1f * static_cast<float>(i % 5));
    }

    preview_.assign(protocol::FRAME_MAX_PAYLOAD, 0);
    uint8_t *out = preview_.data();
    out          = protocol::encode_settings(out, settings);
    for (const auto &panel : panels)
    {
        out = protocol::encode_panel(out, panel);
    }
    out = protocol::put_u16(out, signal_count);
    for (uint16_t i = 0; i < signal_count; i++)
    {
        protocol::SignalConfigWire config {};
        in  = protocol::decode_signal_config(in, config);
        out = protocol::encode_signal_config(out, config);
    }
    out = protocol::put_u16(out, signal_count);
    for (float value : values)
    {
        out = protocol::put_f32(out, value);
    }
    preview_.resize(static_cast<size_t>(out - preview_.data()));

    last_preview_ms_ = now;
    send(MsgType::REQ_PREVIEW_CONFIG,
         preview_.data(),
         static_cast<uint16_t>(preview_.size()));
}
