/**
 * @file test_main.cpp
 * @brief Host-buildable coverage for remote/protocol and remote/server:
 * CRC known-answer vector, frame encode/decode round trips (including
 * corrupted-byte resync and stalled-frame timeout), RemoteConfigServer's
 * REQ_GET_STATE/REQ_APPLY_CONFIG handling (byte-exact field assertions and
 * validation/clamping), and RemoteConfigLink/RemoteApController against a
 * fake ISerialLink. Run with `pio test -e simulator`.
 */
#include <string.h>
#include <unity.h>

#include "remote_ap_controller.hpp"
#include "remote_config_link.hpp"
#include "remote_config_server.hpp"
#include "remote_protocol.hpp"

namespace
{
    using namespace remote::protocol;
    using namespace remote::server;

    /**
     * In-memory IConfigStore test double, same pattern test_core_config
     * already uses — avoids touching sim_config.bin. */
    class FakeConfigStore : public IConfigStore
    {
        public:
            static constexpr size_t CAPACITY = 2048;

            FakeConfigStore()
            {
                memset(bytes_, 0xFF, sizeof(bytes_));
            }

            bool read(uint32_t offset, void *buf, size_t len) override
            {
                if (offset + len > CAPACITY)
                {
                    return false;
                }
                memcpy(buf, bytes_ + offset, len);
                return true;
            }

            bool write(uint32_t offset, const void *buf, size_t len) override
            {
                if (offset + len > CAPACITY)
                {
                    return false;
                }
                memcpy(bytes_ + offset, buf, len);
                return true;
            }

            size_t capacity() const override
            {
                return CAPACITY;
            }

        private:
            uint8_t bytes_[CAPACITY];
    };

    /**
     * Deterministic IClock test double, same pattern test_core_vehicle
     * already uses. */
    class FakeClock : public IClock
    {
        public:
            uint32_t millis() const override
            {
                return now_ms;
            }

            uint32_t now_ms = 0;
    };

    /**
     * ISerialLink test double: a byte queue to "receive" and a capture
     * buffer for what gets "sent". */
    class FakeSerialLink : public ISerialLink
    {
        public:
            void begin(uint32_t) override
            {
            }

            size_t read(uint8_t *buf, size_t len) override
            {
                size_t n = 0;
                while (n < len && read_pos_ < read_len_)
                {
                    buf[n++] = read_buf_[read_pos_++];
                }
                return n;
            }

            size_t write(const uint8_t *buf, size_t len) override
            {
                for (size_t i = 0; i < len && write_len_ < sizeof(write_buf_);
                     i++)
                {
                    write_buf_[write_len_++] = buf[i];
                }
                return len;
            }

            void queue_read(const uint8_t *data, size_t len)
            {
                memcpy(read_buf_ + read_len_, data, len);
                read_len_ += len;
            }

            void clear_write()
            {
                write_len_ = 0;
            }

            uint8_t read_buf_[4096] = {};
            size_t  read_len_       = 0;
            size_t  read_pos_       = 0;

            uint8_t write_buf_[4096] = {};
            size_t  write_len_       = 0;
    };

    /**
     * Feeds a fully-encoded frame through a fresh FrameReader and returns
     * the decoded frame — used to check what a RemoteConfigLink actually
     * wrote onto the wire. */
    DecodedFrame decode_one_frame(const uint8_t *bytes, size_t len)
    {
        static FrameReader reader; /**
                                    * static: DecodedFrame::payload aliases
                                    * its internal buffer, must outlive
                                    * the caller's use of the result */
        reader = FrameReader {};
        for (size_t i = 0; i < len; i++)
        {
            if (reader.on_byte(bytes[i], 0))
            {
                return reader.decoded_frame();
            }
        }
        return {};
    }
} // namespace

void setUp()
{
}
void tearDown()
{
}

/**
 * ==============================================================================
 * CRC
 =============================================================================*/

void test_crc16_known_answer_vector()
{
    const uint8_t check[] = "123456789";
    TEST_ASSERT_EQUAL_UINT16(0x29B1, crc16_ccitt(check, 9));
}

/**
 * ==============================================================================
 * FRAMING
 =============================================================================*/

void test_encode_decode_frame_round_trip()
{
    const uint8_t payload[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    uint8_t       frame_bytes[32];
    size_t        frame_len = encode_frame(MsgType::REQ_GET_STATE,
                                    payload,
                                    sizeof(payload),
                                    frame_bytes,
                                    sizeof(frame_bytes));
    TEST_ASSERT_TRUE(frame_len > 0);

    FrameReader reader;
    bool        completed = false;
    for (size_t i = 0; i < frame_len; i++)
    {
        completed = reader.on_byte(frame_bytes[i], 0);
    }
    TEST_ASSERT_TRUE(completed);

    const DecodedFrame &frame = reader.decoded_frame();
    TEST_ASSERT_TRUE(MsgType::REQ_GET_STATE == frame.type);
    TEST_ASSERT_EQUAL_UINT16(sizeof(payload), frame.payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, frame.payload, sizeof(payload));
}

void test_zero_length_payload_frame_round_trips()
{
    uint8_t frame_bytes[16];
    size_t  frame_len = encode_frame(
        MsgType::CMD_AP_DISABLE, nullptr, 0, frame_bytes, sizeof(frame_bytes));
    TEST_ASSERT_TRUE(frame_len > 0);

    FrameReader reader;
    bool        completed = false;
    for (size_t i = 0; i < frame_len; i++)
    {
        completed = reader.on_byte(frame_bytes[i], 0);
    }
    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_TRUE(MsgType::CMD_AP_DISABLE == reader.decoded_frame().type);
    TEST_ASSERT_EQUAL_UINT16(0, reader.decoded_frame().payload_len);
}

void test_corrupted_frame_resyncs_on_next_frame()
{
    const uint8_t payload[] = { 0xAA, 0xBB };
    uint8_t       frame_bytes[16];
    size_t        frame_len = encode_frame(MsgType::REQ_GET_STATE,
                                    payload,
                                    sizeof(payload),
                                    frame_bytes,
                                    sizeof(frame_bytes));

    FrameReader reader;

    /* Corrupt one payload byte so the CRC fails. */
    uint8_t corrupted[16];
    memcpy(corrupted, frame_bytes, frame_len);
    corrupted[4] ^= 0xFF; /* sync,type,lenlo,lenhi,payload[0] */

    bool completed = false;
    for (size_t i = 0; i < frame_len; i++)
    {
        completed = reader.on_byte(corrupted[i], 0);
    }
    TEST_ASSERT_FALSE(completed); /* CRC mismatch, silently dropped */

    /**
     * A subsequent real frame must still decode correctly — the corrupt
     * frame can't wedge the reader. */
    completed = false;
    for (size_t i = 0; i < frame_len; i++)
    {
        completed = reader.on_byte(frame_bytes[i], 0);
    }
    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_UINT16(sizeof(payload),
                             reader.decoded_frame().payload_len);
}

void test_stray_bytes_before_sync_are_ignored()
{
    const uint8_t payload[] = { 0x42 };
    uint8_t       frame_bytes[16];
    size_t        frame_len = encode_frame(MsgType::REQ_GET_STATE,
                                    payload,
                                    sizeof(payload),
                                    frame_bytes,
                                    sizeof(frame_bytes));

    FrameReader reader;
    bool        completed = false;
    completed |= reader.on_byte(0x00, 0);
    completed |= reader.on_byte(0xFF, 0);
    completed |= reader.on_byte(0x7E, 0);
    TEST_ASSERT_FALSE(completed);

    for (size_t i = 0; i < frame_len; i++)
    {
        completed = reader.on_byte(frame_bytes[i], 0);
    }
    TEST_ASSERT_TRUE(completed);
}

void test_stalled_frame_times_out()
{
    const uint8_t payload[] = { 0x01, 0x02, 0x03 };
    uint8_t       frame_bytes[16];
    size_t        frame_len = encode_frame(MsgType::REQ_GET_STATE,
                                    payload,
                                    sizeof(payload),
                                    frame_bytes,
                                    sizeof(frame_bytes));

    FrameReader reader;
    /**
     * Feed everything up to (not including) the last payload byte, then
     * stall past FRAME_TIMEOUT_MS before finishing it. */
    for (size_t i = 0; i < frame_len - 3; i++)
    {
        reader.on_byte(frame_bytes[i], 0);
    }

    bool completed
        = reader.on_byte(frame_bytes[frame_len - 3], FRAME_TIMEOUT_MS + 1);
    TEST_ASSERT_FALSE(completed);

    /* The reader must have reset — a fresh frame starting now succeeds. */
    completed = false;
    for (size_t i = 0; i < frame_len; i++)
    {
        completed = reader.on_byte(
            frame_bytes[i], FRAME_TIMEOUT_MS + 1 + static_cast<uint32_t>(i));
    }
    TEST_ASSERT_TRUE(completed);
}

/**
 * ==============================================================================
 * REMOTECONFIGSERVER — REQ_GET_STATE
 =============================================================================*/

void test_get_state_reports_defaults()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);

    uint8_t resp[FRAME_MAX_PAYLOAD];
    MsgType resp_type;
    size_t  resp_len = server.handle_request(
        MsgType::REQ_GET_STATE, nullptr, 0, resp, sizeof(resp), resp_type);

    TEST_ASSERT_TRUE(MsgType::RESP_STATE == resp_type);
    TEST_ASSERT_TRUE(resp_len > 0);

    const uint8_t *p = resp;

    SettingsWire settings {};
    p = decode_settings(p, settings);
    TEST_ASSERT_EQUAL_UINT8(100, settings.day_brightness);
    TEST_ASSERT_EQUAL_UINT8(20, settings.night_brightness);
    TEST_ASSERT_EQUAL_UINT8(5, settings.num_panels);
    TEST_ASSERT_EQUAL_UINT8(0, settings.orientation); /* Horizontal */

    uint8_t panel_capacity = 0;
    p                      = get_u8(p, panel_capacity);
    TEST_ASSERT_EQUAL_UINT8(core::MAX_PANELS, panel_capacity);

    PanelWire priorities[core::MAX_PANELS];
    for (uint8_t i = 0; i < core::MAX_PANELS; i++)
    {
        p = decode_panel(p, priorities[i]);
    }
    TEST_ASSERT_EQUAL_UINT16(0, priorities[0].signal_id);  /* Coolant Temp */
    TEST_ASSERT_EQUAL_UINT16(12, priorities[1].signal_id); /* Throttle Pos */

    uint16_t signal_count = 0;
    p                     = get_u16(p, signal_count);
    TEST_ASSERT_EQUAL_UINT16(core::SIGNAL_CATALOG_COUNT, signal_count);

    SignalCatalogEntryWire entry0 {};
    p = decode_signal_catalog_entry(p, entry0);
    TEST_ASSERT_EQUAL_UINT16(core::SIGNAL_CATALOG[0].id, entry0.id);
    TEST_ASSERT_EQUAL_STRING(core::SIGNAL_CATALOG[0].name, entry0.name);
    TEST_ASSERT_EQUAL_FLOAT(core::SIGNAL_CATALOG[0].display_min,
                            entry0.display_min);
    TEST_ASSERT_EQUAL_FLOAT(core::SIGNAL_CATALOG[0].display_max,
                            entry0.display_max);
    for (size_t i = 1; i < signal_count; i++)
    {
        SignalCatalogEntryWire entry {};
        p = decode_signal_catalog_entry(p, entry);
    }

    SignalConfigWire sig0 {};
    p = decode_signal_config(p, sig0);
    TEST_ASSERT_EQUAL_UINT8(0, sig0.use_lo);
    TEST_ASSERT_EQUAL_UINT8(0, sig0.use_hi);
    TEST_ASSERT_EQUAL_UINT8(0, sig0.use_custom_range);

    TEST_ASSERT_EQUAL_UINT32(
        static_cast<uint32_t>(p - resp) /* consumed so far */
            + static_cast<uint32_t>(signal_count - 1) * 19,
        static_cast<uint32_t>(resp_len));
}

/**
 * ==============================================================================
 * REMOTECONFIGSERVER — REQ_APPLY_CONFIG
 =============================================================================*/

namespace
{
    /**
     * Builds a valid REQ_APPLY_CONFIG payload matching the current
     * defaults, so individual tests only need to override what they're
     * testing. */
    size_t build_apply_payload(const core::Config &defaults,
                               uint8_t            *out,
                               float              *lo_override   = nullptr,
                               float              *hi_override   = nullptr,
                               uint16_t            bad_signal_id = 0xFFFF,
                               bool                use_custom    = false,
                               float               custom_min    = 0.0f,
                               float               custom_max    = 0.0f)
    {
        uint8_t *p = out;

        SettingsWire settings {};
        settings.day_brightness   = defaults.settings.day_brightness;
        settings.night_brightness = defaults.settings.night_brightness;
        settings.num_panels       = defaults.settings.num_panels;
        settings.orientation      = 0;
        settings.accent_color     = defaults.settings.accent_color;
        p                         = encode_settings(p, settings);

        for (uint8_t i = 0; i < core::MAX_PANELS; i++)
        {
            PanelWire pw {};
            pw.signal_id = (bad_signal_id != 0xFFFF && i == 0)
                               ? bad_signal_id
                               : defaults.priorities[i].signal_id;
            pw.units
                = defaults.priorities[i].units == core::UnitSystem::Imperial
                      ? 1
                      : 0;
            pw.display_mode
                = static_cast<uint8_t>(defaults.priorities[i].display_mode);
            p = encode_panel(p, pw);
        }

        p = put_u16(p, static_cast<uint16_t>(core::SIGNAL_CATALOG_COUNT));
        for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
        {
            SignalConfigWire sw {};
            sw.lo               = (i == 0 && lo_override != nullptr)
                                      ? *lo_override
                                      : defaults.signals[i].warn.lo;
            sw.hi               = (i == 0 && hi_override != nullptr)
                                      ? *hi_override
                                      : defaults.signals[i].warn.hi;
            sw.use_lo           = (i == 0 && lo_override != nullptr) ? 1 : 0;
            sw.use_hi           = (i == 0 && hi_override != nullptr) ? 1 : 0;
            sw.use_custom_range = (i == 0 && use_custom) ? 1 : 0;
            sw.custom_min       = (i == 0) ? custom_min : 0.0f;
            sw.custom_max       = (i == 0) ? custom_max : 0.0f;
            p                   = encode_signal_config(p, sw);
        }

        return static_cast<size_t>(p - out);
    }
} // namespace

void test_apply_config_persists_valid_batch()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);

    uint8_t req[FRAME_MAX_PAYLOAD];
    size_t  req_len = build_apply_payload(config.get(), req);

    uint8_t resp[16];
    MsgType resp_type;
    server.handle_request(MsgType::REQ_APPLY_CONFIG,
                          req,
                          static_cast<uint16_t>(req_len),
                          resp,
                          sizeof(resp),
                          resp_type);

    TEST_ASSERT_TRUE(MsgType::RESP_APPLY_RESULT == resp_type);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ApplyResult::Ok), resp[0]);

    /**
     * Persisted for real — a fresh ConfigStore over the same backing sees
     * it. */
    core::ConfigStore reloaded(backing);
    reloaded.load();
    TEST_ASSERT_EQUAL_UINT8(100, reloaded.get().settings.day_brightness);
}

void test_apply_config_rejects_signal_count_mismatch()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);

    uint8_t      req[FRAME_MAX_PAYLOAD];
    uint8_t     *p = req;
    SettingsWire settings {};
    p = encode_settings(p, settings);
    for (uint8_t i = 0; i < core::MAX_PANELS; i++)
    {
        p = encode_panel(p, PanelWire {});
    }
    p = put_u16(p, 1); /* wrong — should be SIGNAL_CATALOG_COUNT */
    SignalConfigWire sw {};
    p = encode_signal_config(p, sw);

    uint8_t resp[16];
    MsgType resp_type;
    server.handle_request(MsgType::REQ_APPLY_CONFIG,
                          req,
                          static_cast<uint16_t>(p - req),
                          resp,
                          sizeof(resp),
                          resp_type);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ApplyResult::ErrSignalCountMismatch), resp[0]);
    /* Untouched — still whatever load() defaulted to. */
    TEST_ASSERT_EQUAL_UINT8(100, config.get().settings.day_brightness);
}

void test_apply_config_rejects_invalid_signal_id()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);

    uint8_t req[FRAME_MAX_PAYLOAD];
    size_t  req_len = build_apply_payload(
        config.get(), req, nullptr, nullptr, /*bad_signal_id=*/9999);

    uint8_t resp[16];
    MsgType resp_type;
    server.handle_request(MsgType::REQ_APPLY_CONFIG,
                          req,
                          static_cast<uint16_t>(req_len),
                          resp,
                          sizeof(resp),
                          resp_type);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ApplyResult::ErrInvalidSignal),
                            resp[0]);
    TEST_ASSERT_EQUAL_UINT16(
        0, config.get().priorities[0].signal_id); /* untouched */
}

void test_apply_config_clamps_brightness_and_panel_count()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);

    core::Config defaults              = config.get();
    defaults.settings.day_brightness   = 255; /* out of range */
    defaults.settings.night_brightness = 0;
    defaults.settings.num_panels       = 200; /* out of range */

    uint8_t req[FRAME_MAX_PAYLOAD];
    size_t  req_len = build_apply_payload(defaults, req);

    uint8_t resp[16];
    MsgType resp_type;
    server.handle_request(MsgType::REQ_APPLY_CONFIG,
                          req,
                          static_cast<uint16_t>(req_len),
                          resp,
                          sizeof(resp),
                          resp_type);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ApplyResult::Ok), resp[0]);
    TEST_ASSERT_EQUAL_UINT8(100, config.get().settings.day_brightness);
    TEST_ASSERT_EQUAL_UINT8(0, config.get().settings.night_brightness);
    TEST_ASSERT_EQUAL_UINT8(core::MAX_PANELS, config.get().settings.num_panels);
}

void test_apply_config_clamps_thresholds_to_effective_range()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);

    /**
     * Signal 0 (Coolant Temp): catalog range is -40..130. Submit a custom
     * range (0..50) *and* thresholds outside it in the same batch — the
     * thresholds must clamp against the custom range just submitted, not
     * the catalog default. */
    float   lo = -100.0f;
    float   hi = 500.0f;
    uint8_t req[FRAME_MAX_PAYLOAD];
    size_t  req_len = build_apply_payload(config.get(),
                                         req,
                                         &lo,
                                         &hi,
                                         0xFFFF,
                                         /*use_custom=*/true,
                                         /*custom_min=*/0.0f,
                                         /*custom_max=*/50.0f);

    uint8_t resp[16];
    MsgType resp_type;
    server.handle_request(MsgType::REQ_APPLY_CONFIG,
                          req,
                          static_cast<uint16_t>(req_len),
                          resp,
                          sizeof(resp),
                          resp_type);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ApplyResult::Ok), resp[0]);
    TEST_ASSERT_TRUE(config.get().signals[0].use_custom_range);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, config.get().signals[0].custom_min);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, config.get().signals[0].custom_max);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, config.get().signals[0].warn.lo);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, config.get().signals[0].warn.hi);
}

void test_apply_config_swaps_reversed_thresholds()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);

    float   lo = 90.0f; /* lo > hi, both otherwise in-range */
    float   hi = 10.0f;
    uint8_t req[FRAME_MAX_PAYLOAD];
    size_t  req_len = build_apply_payload(config.get(), req, &lo, &hi);

    uint8_t resp[16];
    MsgType resp_type;
    server.handle_request(MsgType::REQ_APPLY_CONFIG,
                          req,
                          static_cast<uint16_t>(req_len),
                          resp,
                          sizeof(resp),
                          resp_type);

    TEST_ASSERT_TRUE(config.get().signals[0].warn.lo
                     <= config.get().signals[0].warn.hi);
}

/**
 * ==============================================================================
 * REMOTECONFIGLINK / REMOTEAPCONTROLLER
 =============================================================================*/

void test_link_dispatches_request_and_writes_response()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);
    FakeSerialLink     link;
    RemoteConfigLink   remote_link(link, server);

    uint8_t req_frame[16];
    size_t  req_len = encode_frame(
        MsgType::REQ_GET_STATE, nullptr, 0, req_frame, sizeof(req_frame));
    link.queue_read(req_frame, req_len);

    remote_link.poll(nullptr, nullptr, 0);

    TEST_ASSERT_TRUE(link.write_len_ > 0);
    DecodedFrame resp = decode_one_frame(link.write_buf_, link.write_len_);
    TEST_ASSERT_TRUE(MsgType::RESP_STATE == resp.type);
}

namespace
{
    struct EvtCapture
    {
            MsgType  type = MsgType::RESP_ERROR;
            uint16_t len  = 0;
            bool     seen = false;
    };

    void capture_evt_cb(void *ctx, MsgType type, const uint8_t *, uint16_t len)
    {
        auto *out = static_cast<EvtCapture *>(ctx);
        out->type = type;
        out->len  = len;
        out->seen = true;
    }
} // namespace

void test_ap_controller_activate_sends_command_and_reaches_active()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);
    FakeSerialLink     link;
    RemoteConfigLink   remote_link(link, server);
    FakeClock          clock;
    RemoteApController controller(remote_link, clock);

    controller.activate();
    TEST_ASSERT_TRUE(ApState::Activating == controller.state());

    DecodedFrame sent = decode_one_frame(link.write_buf_, link.write_len_);
    TEST_ASSERT_TRUE(MsgType::CMD_AP_ENABLE == sent.type);

    ApEnableWire wire {};
    decode_ap_enable(sent.payload, wire);
    TEST_ASSERT_EQUAL_STRING("MQB-Monitor-Setup", wire.ssid);
    /* Deliberately open AP — see RemoteApController's file doc comment. */
    TEST_ASSERT_EQUAL_STRING("", wire.password);

    /**
     * Simulate the C3 replying, via the same poll()/evt_cb path a real
     * link would use. */
    link.clear_write();
    uint8_t     evt_frame[16];
    ApReadyWire ready { { 192, 168, 4, 1 } };
    uint8_t     ready_payload[sizeof(ApReadyWire::ip)];
    encode_ap_ready(ready_payload, ready);
    size_t evt_len = encode_frame(MsgType::EVT_AP_READY,
                                  ready_payload,
                                  sizeof(ready_payload),
                                  evt_frame,
                                  sizeof(evt_frame));
    link.queue_read(evt_frame, evt_len);

    EvtCapture capture;
    remote_link.poll(capture_evt_cb, &capture, 0);
    TEST_ASSERT_TRUE(capture.seen);

    controller.on_event(capture.type, evt_frame + 4, capture.len, 1000);
    TEST_ASSERT_TRUE(ApState::Active == controller.state());
    TEST_ASSERT_EQUAL_STRING("192.168.4.1", controller.ip_text());
}

void test_ap_controller_idle_timeout_disables()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);
    FakeSerialLink     link;
    RemoteConfigLink   remote_link(link, server);
    FakeClock          clock;
    RemoteApController controller(remote_link, clock);

    controller.activate();
    ApReadyWire wire { { 10, 0, 0, 1 } };
    uint8_t     payload[sizeof(ApReadyWire::ip)];
    encode_ap_ready(payload, wire);
    controller.on_event(MsgType::EVT_AP_READY, payload, sizeof(payload), 0);
    TEST_ASSERT_TRUE(ApState::Active == controller.state());

    controller.tick(AP_IDLE_TIMEOUT_MS - 1);
    TEST_ASSERT_TRUE(ApState::Active == controller.state());

    controller.tick(AP_IDLE_TIMEOUT_MS + 1);
    TEST_ASSERT_TRUE(ApState::Deactivating == controller.state());

    /**
     * No EVT_AP_DISABLED ever arrives — the grace-period fallback must
     * still return to Idle rather than staying wedged. */
    controller.tick(AP_IDLE_TIMEOUT_MS + 1 + AP_DISABLE_GRACE_MS + 1);
    TEST_ASSERT_TRUE(ApState::Idle == controller.state());
}

void test_ap_controller_activate_timeout_falls_back_to_idle()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);
    FakeSerialLink     link;
    RemoteConfigLink   remote_link(link, server);
    FakeClock          clock;
    RemoteApController controller(remote_link, clock);

    controller.activate();
    TEST_ASSERT_TRUE(ApState::Activating == controller.state());

    controller.tick(AP_ACTIVATE_TIMEOUT_MS + 1);
    TEST_ASSERT_TRUE(ApState::Idle == controller.state());
}

void test_ap_controller_reactivate_from_deactivating_resends_enable()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);
    FakeSerialLink     link;
    RemoteConfigLink   remote_link(link, server);
    FakeClock          clock;
    RemoteApController controller(remote_link, clock);

    controller.activate();
    ApReadyWire ready_wire { { 10, 0, 0, 1 } };
    uint8_t     ready_payload[sizeof(ApReadyWire::ip)];
    encode_ap_ready(ready_payload, ready_wire);
    controller.on_event(
        MsgType::EVT_AP_READY, ready_payload, sizeof(ready_payload), 0);
    TEST_ASSERT_TRUE(ApState::Active == controller.state());

    controller.deactivate();
    TEST_ASSERT_TRUE(ApState::Deactivating == controller.state());

    /**
     * Reopening Wireless Setup before EVT_AP_DISABLED (or the grace-period
     * fallback) arrives — the screen's own close (X) button doesn't wait
     * around for it, so this is a real workflow, not just a race. Must
     * resend CMD_AP_ENABLE rather than silently no-op and strand the
     * screen on its spinner. */
    link.clear_write();
    controller.activate();
    TEST_ASSERT_TRUE(ApState::Activating == controller.state());

    DecodedFrame resent = decode_one_frame(link.write_buf_, link.write_len_);
    TEST_ASSERT_TRUE(MsgType::CMD_AP_ENABLE == resent.type);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_crc16_known_answer_vector);
    RUN_TEST(test_encode_decode_frame_round_trip);
    RUN_TEST(test_zero_length_payload_frame_round_trips);
    RUN_TEST(test_corrupted_frame_resyncs_on_next_frame);
    RUN_TEST(test_stray_bytes_before_sync_are_ignored);
    RUN_TEST(test_stalled_frame_times_out);
    RUN_TEST(test_get_state_reports_defaults);
    RUN_TEST(test_apply_config_persists_valid_batch);
    RUN_TEST(test_apply_config_rejects_signal_count_mismatch);
    RUN_TEST(test_apply_config_rejects_invalid_signal_id);
    RUN_TEST(test_apply_config_clamps_brightness_and_panel_count);
    RUN_TEST(test_apply_config_clamps_thresholds_to_effective_range);
    RUN_TEST(test_apply_config_swaps_reversed_thresholds);
    RUN_TEST(test_link_dispatches_request_and_writes_response);
    RUN_TEST(test_ap_controller_activate_sends_command_and_reaches_active);
    RUN_TEST(test_ap_controller_idle_timeout_disables);
    RUN_TEST(test_ap_controller_activate_timeout_falls_back_to_idle);
    RUN_TEST(test_ap_controller_reactivate_from_deactivating_resends_enable);
    return UNITY_END();
}
