/**
 * @file test_main.cpp
 * @brief Host-buildable coverage for remote/protocol and remote/server:
 * CRC known-answer vector, frame encode/decode round trips (including
 * corrupted-byte resync and stalled-frame timeout), RemoteConfigServer's
 * REQ_GET_STATE/REQ_APPLY_CONFIG handling (byte-exact field assertions and
 * validation/clamping), and RemoteConfigLink/RemoteApController/
 * RemoteOtaController against a fake ISerialLink. Run with
 * `pio test -e simulator`.
 */
#include <math.h>
#include <string.h>
#include <unity.h>

#include "remote_ap_controller.hpp"
#include "remote_config_link.hpp"
#include "remote_config_server.hpp"
#include "remote_ota_controller.hpp"
#include "remote_protocol.hpp"

namespace
{
    using namespace remote::protocol;
    using namespace remote::server;

    /* In-memory IConfigStore test double, same pattern test_core_config
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

    /* Deterministic IClock test double, same pattern test_core_vehicle
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

    /* ISerialLink test double: a byte queue to "receive" and a capture
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

    /* Feeds a fully-encoded frame through a fresh FrameReader and returns
     * the decoded frame — used to check what a RemoteConfigLink actually
     * wrote onto the wire. */
    DecodedFrame decode_one_frame(const uint8_t *bytes, size_t len)
    {
        static FrameReader reader; /* static: DecodedFrame::payload aliases
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

/*==============================================================================
 * CRC
 =============================================================================*/

void test_crc16_known_answer_vector()
{
    const uint8_t check[] = "123456789";
    TEST_ASSERT_EQUAL_UINT16(0x29B1, crc16_ccitt(check, 9));
}

/*==============================================================================
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

    /* A subsequent real frame must still decode correctly — the corrupt
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
    /* Feed everything up to (not including) the last payload byte, then
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

/*==============================================================================
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
    TEST_ASSERT_EQUAL_UINT8(0, settings.flips);

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

    for (size_t i = 1; i < signal_count; i++)
    {
        SignalConfigWire sig {};
        p = decode_signal_config(p, sig);
    }

    uint8_t preview_active = 0xFF;
    p                      = get_u8(p, preview_active);
    TEST_ASSERT_EQUAL_UINT8(0, preview_active);

    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(p - resp),
                             static_cast<uint32_t>(resp_len));
}

/*==============================================================================
 * REMOTECONFIGSERVER — REQ_APPLY_CONFIG
 =============================================================================*/

namespace
{
    /* Builds a valid REQ_APPLY_CONFIG payload matching the current
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
        settings.flips            = static_cast<uint8_t>(
            (defaults.settings.flipped_horizontal ? 1 : 0)
            | (defaults.settings.flipped_vertical ? 2 : 0));
        settings.brightness_mode
            = static_cast<uint8_t>(defaults.settings.brightness_mode);
        settings.auto_brightness = defaults.settings.auto_brightness_pct;
        p = encode_settings(p, settings);

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

    /* Persisted for real — a fresh ConfigStore over the same backing sees
     * it. */
    core::ConfigStore reloaded(backing);
    reloaded.load();
    TEST_ASSERT_EQUAL_UINT8(100, reloaded.get().settings.day_brightness);
}

void test_apply_config_round_trips_flips()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);

    /* The settings record's flips byte: only the vertical layout turned. */
    uint8_t req[FRAME_MAX_PAYLOAD];
    size_t  req_len = build_apply_payload(config.get(), req);
    req[8]          = 2;

    uint8_t resp[FRAME_MAX_PAYLOAD];
    MsgType resp_type;
    server.handle_request(MsgType::REQ_APPLY_CONFIG,
                          req,
                          static_cast<uint16_t>(req_len),
                          resp,
                          sizeof(resp),
                          resp_type);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ApplyResult::Ok), resp[0]);

    core::ConfigStore reloaded(backing);
    reloaded.load();
    TEST_ASSERT_FALSE(reloaded.get().settings.flipped_horizontal);
    TEST_ASSERT_TRUE(reloaded.get().settings.flipped_vertical);

    server.handle_request(
        MsgType::REQ_GET_STATE, nullptr, 0, resp, sizeof(resp), resp_type);
    SettingsWire settings {};
    decode_settings(resp, settings);
    TEST_ASSERT_EQUAL_UINT8(2, settings.flips);
}

void test_apply_config_round_trips_brightness_settings()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);

    /* The settings record's mode byte, 2 = Auto, and the Auto slider
     * after it, past its 100 max. */
    uint8_t req[FRAME_MAX_PAYLOAD];
    size_t  req_len = build_apply_payload(config.get(), req);
    req[9]          = 2;
    req[10]         = 150;

    uint8_t resp[FRAME_MAX_PAYLOAD];
    MsgType resp_type;
    server.handle_request(MsgType::REQ_APPLY_CONFIG,
                          req,
                          static_cast<uint16_t>(req_len),
                          resp,
                          sizeof(resp),
                          resp_type);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ApplyResult::Ok), resp[0]);

    core::ConfigStore reloaded(backing);
    reloaded.load();
    TEST_ASSERT_TRUE(core::BrightnessMode::Auto
                     == reloaded.get().settings.brightness_mode);
    TEST_ASSERT_EQUAL_UINT8(100, reloaded.get().settings.auto_brightness_pct);

    server.handle_request(
        MsgType::REQ_GET_STATE, nullptr, 0, resp, sizeof(resp), resp_type);
    SettingsWire settings {};
    decode_settings(resp, settings);
    TEST_ASSERT_EQUAL_UINT8(2, settings.brightness_mode);
    TEST_ASSERT_EQUAL_UINT8(100, settings.auto_brightness);

    /* A mode this firmware doesn't have is refused, and changes nothing. */
    req[9] = 3;
    server.handle_request(MsgType::REQ_APPLY_CONFIG,
                          req,
                          static_cast<uint16_t>(req_len),
                          resp,
                          sizeof(resp),
                          resp_type);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ApplyResult::ErrInvalidEnum),
                            resp[0]);
    TEST_ASSERT_TRUE(core::BrightnessMode::Auto
                     == config.get().settings.brightness_mode);
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

    /* Signal 0 (Coolant Temp): catalog range is -40..130. Submit a custom
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

void test_apply_config_keeps_lone_threshold_on_its_side()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);

    /* High warning at 10, an unused low left at 90 -- the pair is
     * "reversed", but only one side is live, so nothing should swap. */
    float   lo = 90.0f;
    float   hi = 10.0f;
    uint8_t req[FRAME_MAX_PAYLOAD];
    size_t  req_len = build_apply_payload(config.get(), req, &lo, &hi);
    /* Signal 0's record starts after settings (11), the panels (24) and
     * the count (2); use_lo is its 9th byte. */
    req[11 + 24 + 2 + 8] = 0;

    uint8_t resp[16];
    MsgType resp_type;
    server.handle_request(MsgType::REQ_APPLY_CONFIG,
                          req,
                          static_cast<uint16_t>(req_len),
                          resp,
                          sizeof(resp),
                          resp_type);

    TEST_ASSERT_FALSE(config.get().signals[0].warn.use_lo);
    TEST_ASSERT_TRUE(config.get().signals[0].warn.use_hi);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, config.get().signals[0].warn.hi);
}

/*==============================================================================
 * REMOTECONFIGLINK / REMOTEAPCONTROLLER
 =============================================================================*/

/*==============================================================================
 * REMOTECONFIGSERVER — PREVIEW / COMMIT / REVERT
 =============================================================================*/

namespace
{
    struct EventLog
    {
            RemoteConfigServer::ConfigEvent events[8];
            uint8_t                         count = 0;
    };

    void record_event(void *ctx, RemoteConfigServer::ConfigEvent event)
    {
        auto *log = static_cast<EventLog *>(ctx);
        if (log->count < 8)
        {
            log->events[log->count++] = event;
        }
    }

    /* Sends one config-changing request and returns its ApplyResult
     * byte. */
    uint8_t send_change(RemoteConfigServer &server,
                        MsgType             type,
                        const uint8_t      *payload,
                        size_t              len)
    {
        uint8_t resp[16] = { 0xFF };
        MsgType resp_type;
        size_t  resp_len = server.handle_request(type,
                                                payload,
                                                static_cast<uint16_t>(len),
                                                resp,
                                                sizeof(resp),
                                                resp_type);
        TEST_ASSERT_TRUE(MsgType::RESP_APPLY_RESULT == resp_type);
        TEST_ASSERT_EQUAL_UINT32(1, resp_len);
        return resp[0];
    }

    /* A valid batch equal to `cfg` but with day brightness `day`. */
    size_t build_payload_with_day(const core::Config &cfg,
                                  uint8_t             day,
                                  uint8_t            *out)
    {
        core::Config edited            = cfg;
        edited.settings.day_brightness = day;
        return build_apply_payload(edited, out);
    }
} // namespace

void test_preview_changes_live_config_without_saving()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);
    EventLog           log;
    server.set_config_callback(record_event, &log);

    uint8_t req[FRAME_MAX_PAYLOAD];
    size_t  req_len = build_payload_with_day(config.get(), 40, req);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ApplyResult::Ok),
        send_change(server, MsgType::REQ_PREVIEW_CONFIG, req, req_len));

    TEST_ASSERT_EQUAL_UINT8(40, config.get().settings.day_brightness);
    TEST_ASSERT_TRUE(server.preview_active());
    TEST_ASSERT_EQUAL_UINT8(1, log.count);
    TEST_ASSERT_TRUE(RemoteConfigServer::ConfigEvent::Previewed
                     == log.events[0]);

    /* Nothing reached the store. */
    core::ConfigStore reloaded(backing);
    reloaded.load();
    TEST_ASSERT_EQUAL_UINT8(100, reloaded.get().settings.day_brightness);

    /* RESP_STATE's trailing byte reports the session (bit 0) and its
     * unsaved edits (bit 1). */
    uint8_t resp[FRAME_MAX_PAYLOAD];
    MsgType resp_type;
    size_t  resp_len = server.handle_request(
        MsgType::REQ_GET_STATE, nullptr, 0, resp, sizeof(resp), resp_type);
    TEST_ASSERT_EQUAL_UINT8(3, resp[resp_len - 1]);
    TEST_ASSERT_TRUE(server.has_unsaved_changes());

    /* Previewing the saved config again is a session with nothing to
     * save -- what the phone's page sends when it first opens. */
    req_len = build_payload_with_day(config.get(), 100, req);
    send_change(server, MsgType::REQ_PREVIEW_CONFIG, req, req_len);
    TEST_ASSERT_TRUE(server.preview_active());
    TEST_ASSERT_FALSE(server.has_unsaved_changes());
}

void test_apply_during_preview_saves_and_keeps_session()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);
    EventLog           log;
    server.set_config_callback(record_event, &log);

    uint8_t req[FRAME_MAX_PAYLOAD];
    size_t  req_len = build_payload_with_day(config.get(), 40, req);
    send_change(server, MsgType::REQ_PREVIEW_CONFIG, req, req_len);

    req_len = build_payload_with_day(config.get(), 55, req);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ApplyResult::Ok),
        send_change(server, MsgType::REQ_APPLY_CONFIG, req, req_len));

    /* The phone's page is still open, so the session carries on -- only
     * the unsaved flag clears. */
    TEST_ASSERT_TRUE(server.preview_active());
    TEST_ASSERT_FALSE(server.has_unsaved_changes());
    TEST_ASSERT_EQUAL_UINT8(2, log.count);
    TEST_ASSERT_TRUE(RemoteConfigServer::ConfigEvent::Committed
                     == log.events[1]);

    core::ConfigStore reloaded(backing);
    reloaded.load();
    TEST_ASSERT_EQUAL_UINT8(55, reloaded.get().settings.day_brightness);

    /* Ending the session now goes back to the save, not to what was
     * saved before the session began. */
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ApplyResult::Ok),
        send_change(server, MsgType::REQ_REVERT_PREVIEW, nullptr, 0));
    TEST_ASSERT_EQUAL_UINT8(55, config.get().settings.day_brightness);
    TEST_ASSERT_FALSE(server.preview_active());
    TEST_ASSERT_EQUAL_UINT8(3, log.count);

    /* With no session, a revert is a no-op. */
    send_change(server, MsgType::REQ_REVERT_PREVIEW, nullptr, 0);
    TEST_ASSERT_EQUAL_UINT8(3, log.count);
}

void test_preview_values_replace_live_ones_for_the_session()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);
    TEST_ASSERT_NULL(server.preview_values());

    /* The batch, then one value per signal: 87.5 for the first, NaN (no
     * value) for the second, 0 for the rest. */
    uint8_t  req[FRAME_MAX_PAYLOAD];
    size_t   req_len = build_apply_payload(config.get(), req);
    uint8_t *p       = req + req_len;
    p = put_u16(p, static_cast<uint16_t>(core::SIGNAL_CATALOG_COUNT));
    for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
    {
        p = put_f32(p, i == 0 ? 87.5f : (i == 1 ? NAN : 0.0f));
    }
    send_change(server,
                MsgType::REQ_PREVIEW_CONFIG,
                req,
                static_cast<size_t>(p - req));

    const core::SignalStore *values = server.preview_values();
    TEST_ASSERT_NOT_NULL(values);
    TEST_ASSERT_TRUE(values->get_at(0).has_data);
    TEST_ASSERT_EQUAL_FLOAT(87.5f, values->get_at(0).value);
    TEST_ASSERT_FALSE(values->get_at(1).has_data);
    TEST_ASSERT_TRUE(values->get_at(2).has_data);

    /* A preview without values shows none. */
    send_change(server, MsgType::REQ_PREVIEW_CONFIG, req, req_len);
    TEST_ASSERT_FALSE(server.preview_values()->get_at(0).has_data);

    server.revert_preview();
    TEST_ASSERT_NULL(server.preview_values());
}

void test_revert_restores_config_from_before_first_preview()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);
    EventLog           log;
    server.set_config_callback(record_event, &log);

    /* Two previews in a row: the revert target is the saved config, not
     * the first preview. */
    uint8_t req[FRAME_MAX_PAYLOAD];
    size_t  req_len = build_payload_with_day(config.get(), 40, req);
    send_change(server, MsgType::REQ_PREVIEW_CONFIG, req, req_len);
    req_len = build_payload_with_day(config.get(), 10, req);
    send_change(server, MsgType::REQ_PREVIEW_CONFIG, req, req_len);
    TEST_ASSERT_EQUAL_UINT8(10, config.get().settings.day_brightness);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ApplyResult::Ok),
        send_change(server, MsgType::REQ_REVERT_PREVIEW, nullptr, 0));

    TEST_ASSERT_EQUAL_UINT8(100, config.get().settings.day_brightness);
    TEST_ASSERT_FALSE(server.preview_active());
    TEST_ASSERT_EQUAL_UINT8(3, log.count);
    TEST_ASSERT_TRUE(RemoteConfigServer::ConfigEvent::Reverted
                     == log.events[2]);

    /* The composition root's path (AP going down) behaves the same, and
     * is a no-op once nothing is pending. */
    req_len = build_payload_with_day(config.get(), 30, req);
    send_change(server, MsgType::REQ_PREVIEW_CONFIG, req, req_len);
    server.revert_preview();
    server.revert_preview();
    TEST_ASSERT_EQUAL_UINT8(100, config.get().settings.day_brightness);
    TEST_ASSERT_EQUAL_UINT8(5, log.count);

    /* The page closing reverts the same, but says so. */
    req_len = build_payload_with_day(config.get(), 20, req);
    send_change(server, MsgType::REQ_PREVIEW_CONFIG, req, req_len);
    uint8_t left = REVERT_PAGE_LEFT;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ApplyResult::Ok),
        send_change(server, MsgType::REQ_REVERT_PREVIEW, &left, 1));
    TEST_ASSERT_EQUAL_UINT8(100, config.get().settings.day_brightness);
    TEST_ASSERT_FALSE(server.preview_active());
    TEST_ASSERT_EQUAL_UINT8(7, log.count);
    TEST_ASSERT_TRUE(RemoteConfigServer::ConfigEvent::PageLeft
                     == log.events[6]);
}

void test_rejected_preview_leaves_live_config_untouched()
{
    FakeConfigStore   backing;
    core::ConfigStore config(backing);
    config.load();
    core::SignalStore signals;

    RemoteConfigServer server(config, signals);
    EventLog           log;
    server.set_config_callback(record_event, &log);

    uint8_t req[FRAME_MAX_PAYLOAD];
    size_t  req_len = build_apply_payload(config.get(), req, nullptr, nullptr,
                                         /* bad_signal_id */ 9999);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ApplyResult::ErrInvalidSignal),
        send_change(server, MsgType::REQ_PREVIEW_CONFIG, req, req_len));

    TEST_ASSERT_FALSE(server.preview_active());
    TEST_ASSERT_EQUAL_UINT8(0, log.count);

    /* Cut short mid-signal-list. */
    req_len = build_apply_payload(config.get(), req);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ApplyResult::ErrTruncated),
        send_change(server, MsgType::REQ_PREVIEW_CONFIG, req, req_len - 1));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ApplyResult::ErrTruncated),
        send_change(server, MsgType::REQ_APPLY_CONFIG, req, 3));
    TEST_ASSERT_EQUAL_UINT8(0, log.count);
}

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

    /* Simulate the C3 replying, via the same poll()/evt_cb path a real
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

    /* No EVT_AP_DISABLED ever arrives — the grace-period fallback must
     * still return to Idle rather than staying wedged. */
    controller.tick(AP_IDLE_TIMEOUT_MS + 1 + AP_DISABLE_GRACE_MS + 1);
    TEST_ASSERT_TRUE(ApState::Idle == controller.state());
}

void test_ap_controller_activity_restarts_idle_timeout()
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

    /* A phone still editing near the end of the window keeps it open. */
    controller.note_activity(AP_IDLE_TIMEOUT_MS - 10);
    controller.tick(AP_IDLE_TIMEOUT_MS + 1);
    TEST_ASSERT_TRUE(ApState::Active == controller.state());

    controller.tick(2 * AP_IDLE_TIMEOUT_MS);
    TEST_ASSERT_TRUE(ApState::Deactivating == controller.state());
}

void test_ap_controller_counts_stations_only_while_active()
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

    /* Before the AP is up, a stray count is ignored. */
    uint8_t one = 1;
    controller.activate();
    controller.on_event(MsgType::EVT_AP_STATIONS, &one, 1, 0);
    TEST_ASSERT_EQUAL_UINT8(0, controller.station_count());

    ApReadyWire wire { { 10, 0, 0, 1 } };
    uint8_t     payload[sizeof(ApReadyWire::ip)];
    encode_ap_ready(payload, wire);
    controller.on_event(MsgType::EVT_AP_READY, payload, sizeof(payload), 0);
    TEST_ASSERT_EQUAL_UINT8(0, controller.station_count());

    controller.on_event(MsgType::EVT_AP_STATIONS, &one, 1, 100);
    TEST_ASSERT_EQUAL_UINT8(1, controller.station_count());

    /* A phone joining is activity: the idle timeout runs from it. */
    controller.tick(AP_IDLE_TIMEOUT_MS + 1);
    TEST_ASSERT_TRUE(ApState::Active == controller.state());

    /* Going down forgets the phones, and coming back up starts at 0. */
    controller.deactivate();
    TEST_ASSERT_EQUAL_UINT8(0, controller.station_count());
    controller.on_event(MsgType::EVT_AP_DISABLED, nullptr, 0, 200);
    controller.activate();
    controller.on_event(MsgType::EVT_AP_READY, payload, sizeof(payload), 300);
    TEST_ASSERT_EQUAL_UINT8(0, controller.station_count());
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

    /* Reopening Wireless Setup before EVT_AP_DISABLED (or the grace-period
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

/*==============================================================================
 * FIRMWARE UPDATE
 =============================================================================*/

namespace
{
    /* Everything a RemoteOtaController needs, wired as main.cpp does. */
    struct OtaFixture
    {
            FakeConfigStore     backing;
            core::ConfigStore   config { backing };
            core::SignalStore   signals;
            RemoteConfigServer  server { config, signals };
            FakeSerialLink      link;
            RemoteConfigLink    remote_link { link, server };
            RemoteOtaController ota { remote_link };

            void begin(uint32_t total, const char *version, uint32_t now_ms)
            {
                OtaBeginWire wire {};
                wire.total_bytes = total;
                strncpy(wire.version, version, sizeof(wire.version) - 1);
                uint8_t payload[OTA_BEGIN_BYTES];
                encode_ota_begin(payload, wire);
                ota.on_event(
                    MsgType::EVT_OTA_BEGIN, payload, sizeof(payload), now_ms);
            }

            void progress(uint32_t written, uint32_t now_ms)
            {
                uint8_t payload[4];
                put_u32(payload, written);
                ota.on_event(MsgType::EVT_OTA_PROGRESS,
                             payload,
                             sizeof(payload),
                             now_ms);
            }

            void result(OtaResult result, uint32_t now_ms)
            {
                uint8_t payload = static_cast<uint8_t>(result);
                ota.on_event(MsgType::EVT_OTA_RESULT, &payload, 1, now_ms);
            }

            void request(uint32_t now_ms)
            {
                ota.on_event(MsgType::EVT_OTA_REQUEST, nullptr, 0, now_ms);
            }

            /* Into update mode, as the panel's Start does, discarding
             * the CMD_OTA_ARM it sends. */
            void arm(uint32_t now_ms)
            {
                request(now_ms);
                ota.accept(now_ms);
                link.clear_write();
            }

            /* The one frame the controller last wrote; clears it. */
            DecodedFrame take_sent()
            {
                DecodedFrame sent
                    = decode_one_frame(link.write_buf_, link.write_len_);
                link.clear_write();
                return sent;
            }
    };
} // namespace

void test_ota_begin_round_trips()
{
    OtaBeginWire in {};
    in.total_bytes = 1220736;
    strncpy(in.version, "v1.2-3-gabc1234-dirty", sizeof(in.version) - 1);

    uint8_t  buf[OTA_BEGIN_BYTES];
    uint8_t *end = encode_ota_begin(buf, in);
    TEST_ASSERT_EQUAL(OTA_BEGIN_BYTES, end - buf);

    OtaBeginWire out {};
    decode_ota_begin(buf, out);
    TEST_ASSERT_EQUAL_UINT32(1220736, out.total_bytes);
    TEST_ASSERT_EQUAL_STRING("v1.2-3-gabc1234-dirty", out.version);

    /* An unterminated version off the wire still decodes to a string. */
    memset(buf + 4, 'x', WIRE_VERSION_LEN);
    decode_ota_begin(buf, out);
    TEST_ASSERT_EQUAL(WIRE_VERSION_LEN - 1, strlen(out.version));
}

void test_ota_controller_request_then_upload_then_install()
{
    OtaFixture f;
    TEST_ASSERT_TRUE(OtaState::Idle == f.ota.state());

    /* The phone asks; nothing goes to the relay until the panel agrees. */
    f.request(0);
    TEST_ASSERT_TRUE(OtaState::Requested == f.ota.state());
    TEST_ASSERT_FALSE(f.ota.in_update_mode());
    TEST_ASSERT_EQUAL(0, f.link.write_len_);

    f.ota.accept(0);
    TEST_ASSERT_TRUE(OtaState::Armed == f.ota.state());
    TEST_ASSERT_TRUE(f.ota.in_update_mode());
    TEST_ASSERT_TRUE(MsgType::CMD_OTA_ARM == f.take_sent().type);

    f.begin(1000, "v2.0", 10);
    TEST_ASSERT_TRUE(OtaState::Receiving == f.ota.state());
    TEST_ASSERT_EQUAL_STRING("v2.0", f.ota.version());
    TEST_ASSERT_EQUAL_UINT8(0, f.ota.percent());

    f.progress(500, 100);
    TEST_ASSERT_EQUAL_UINT8(50, f.ota.percent());

    f.result(OtaResult::Ok, 200);
    TEST_ASSERT_TRUE(OtaState::Ready == f.ota.state());
    TEST_ASSERT_EQUAL_UINT8(100, f.ota.percent());
    TEST_ASSERT_EQUAL(0, f.link.write_len_); /* nothing commits on its own */

    f.ota.install(300);
    TEST_ASSERT_TRUE(OtaState::Installing == f.ota.state());
    DecodedFrame sent = f.take_sent();
    TEST_ASSERT_TRUE(MsgType::CMD_OTA_COMMIT == sent.type);
    TEST_ASSERT_EQUAL_UINT16(0, sent.payload_len);
}

void test_ota_controller_upload_needs_update_mode()
{
    OtaFixture f;

    /* An upload the relay somehow reported outside update mode is not
     * shown, let alone installable. */
    f.begin(1000, "v2.0", 0);
    f.result(OtaResult::Ok, 10);
    TEST_ASSERT_TRUE(OtaState::Idle == f.ota.state());
    f.ota.install(20);
    TEST_ASSERT_EQUAL(0, f.link.write_len_);

    /* Asked but not yet agreed to: still nothing. */
    f.request(30);
    f.begin(1000, "v2.0", 40);
    TEST_ASSERT_TRUE(OtaState::Requested == f.ota.state());
}

void test_ota_controller_decline_and_request_timeout()
{
    OtaFixture f;

    f.request(0);
    f.ota.decline();
    TEST_ASSERT_TRUE(OtaState::Idle == f.ota.state());
    TEST_ASSERT_TRUE(MsgType::CMD_OTA_DISARM == f.take_sent().type);

    /* Nobody answers the popup: it declines on its own. */
    f.request(1000);
    f.ota.tick(1000 + OTA_REQUEST_TIMEOUT_MS - 1);
    TEST_ASSERT_TRUE(OtaState::Requested == f.ota.state());
    f.ota.tick(1000 + OTA_REQUEST_TIMEOUT_MS + 1);
    TEST_ASSERT_TRUE(OtaState::Idle == f.ota.state());
    TEST_ASSERT_TRUE(MsgType::CMD_OTA_DISARM == f.take_sent().type);
}

void test_ota_controller_repeat_request_while_armed_rearms()
{
    OtaFixture f;
    f.arm(0);

    f.request(10);
    TEST_ASSERT_TRUE(OtaState::Armed == f.ota.state());
    TEST_ASSERT_TRUE(MsgType::CMD_OTA_ARM == f.take_sent().type);
}

void test_ota_controller_failed_upload_waits_for_another()
{
    OtaFixture f;
    f.arm(0);

    f.begin(1000, "v2.0", 0);
    f.ota.install(10);
    TEST_ASSERT_TRUE(OtaState::Receiving == f.ota.state());

    f.result(OtaResult::ErrCorrupt, 20);
    TEST_ASSERT_TRUE(OtaState::Armed == f.ota.state());
    TEST_ASSERT_TRUE(OtaResult::ErrCorrupt == f.ota.last_result());
    f.ota.install(30);
    TEST_ASSERT_EQUAL(0, f.link.write_len_);

    /* The next try starts clean. */
    f.begin(1000, "v2.1", 40);
    TEST_ASSERT_TRUE(OtaState::Receiving == f.ota.state());
    TEST_ASSERT_TRUE(OtaResult::Ok == f.ota.last_result());
}

void test_ota_controller_leave_ends_update_mode()
{
    OtaFixture f;

    /* From waiting... */
    f.arm(0);
    f.ota.leave();
    TEST_ASSERT_TRUE(OtaState::Idle == f.ota.state());
    TEST_ASSERT_TRUE(MsgType::CMD_OTA_DISARM == f.take_sent().type);

    /* ...and from Ready, leaving the image unused. */
    f.arm(10);
    f.begin(1000, "v2.0", 10);
    f.result(OtaResult::Ok, 20);
    f.ota.leave();
    TEST_ASSERT_TRUE(OtaState::Idle == f.ota.state());
    TEST_ASSERT_TRUE(MsgType::CMD_OTA_DISARM == f.take_sent().type);

    /* Not mid-upload: there's no clean way out of that. */
    f.arm(30);
    f.begin(1000, "v2.0", 30);
    f.ota.leave();
    TEST_ASSERT_TRUE(OtaState::Receiving == f.ota.state());
    TEST_ASSERT_EQUAL(0, f.link.write_len_);
}

void test_ota_controller_failed_commit_waits_for_another()
{
    OtaFixture f;
    f.arm(0);
    f.begin(1000, "v2.0", 0);
    f.result(OtaResult::Ok, 10);
    f.ota.install(20);
    f.take_sent();

    f.result(OtaResult::ErrFlash, 30);
    TEST_ASSERT_TRUE(OtaState::Armed == f.ota.state());
    TEST_ASSERT_TRUE(OtaResult::ErrFlash == f.ota.last_result());
}

void test_ota_controller_timeouts_fall_back()
{
    OtaFixture f;
    f.arm(0);

    /* Progress keeps a slow upload alive... */
    f.begin(1000, "v2.0", 0);
    f.progress(100, OTA_STALL_TIMEOUT_MS - 1);
    f.ota.tick(OTA_STALL_TIMEOUT_MS + 1);
    TEST_ASSERT_TRUE(OtaState::Receiving == f.ota.state());

    /* ...but one that goes silent is given up on. */
    f.ota.tick(2 * OTA_STALL_TIMEOUT_MS);
    TEST_ASSERT_TRUE(OtaState::Armed == f.ota.state());
    TEST_ASSERT_TRUE(OtaResult::ErrInterrupted == f.ota.last_result());

    /* A commit the board never acts on doesn't strand the panel. */
    f.begin(1000, "v2.0", 0);
    f.result(OtaResult::Ok, 0);
    f.ota.install(0);
    f.ota.tick(OTA_INSTALL_TIMEOUT_MS - 1);
    TEST_ASSERT_TRUE(OtaState::Installing == f.ota.state());
    f.ota.tick(OTA_INSTALL_TIMEOUT_MS + 1);
    TEST_ASSERT_TRUE(OtaState::Armed == f.ota.state());

    /* Ready waits on the person in the car as long as it takes. */
    f.begin(1000, "v2.0", 0);
    f.result(OtaResult::Ok, 0);
    f.ota.tick(10 * OTA_WAIT_TIMEOUT_MS);
    TEST_ASSERT_TRUE(OtaState::Ready == f.ota.state());
}

void test_ota_controller_update_mode_gives_up_waiting()
{
    OtaFixture f;
    f.arm(1000);

    f.ota.tick(1000 + OTA_WAIT_TIMEOUT_MS - 1);
    TEST_ASSERT_TRUE(OtaState::Armed == f.ota.state());
    f.ota.tick(1000 + OTA_WAIT_TIMEOUT_MS + 1);
    TEST_ASSERT_TRUE(OtaState::Idle == f.ota.state());
    TEST_ASSERT_TRUE(MsgType::CMD_OTA_DISARM == f.take_sent().type);

    /* A failed upload restarts the wait. */
    f.arm(0);
    f.begin(1000, "v2.0", 0);
    f.result(OtaResult::ErrCorrupt, OTA_WAIT_TIMEOUT_MS);
    f.ota.tick(OTA_WAIT_TIMEOUT_MS + 10);
    TEST_ASSERT_TRUE(OtaState::Armed == f.ota.state());
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
    RUN_TEST(test_apply_config_round_trips_flips);
    RUN_TEST(test_apply_config_round_trips_brightness_settings);
    RUN_TEST(test_apply_config_rejects_signal_count_mismatch);
    RUN_TEST(test_apply_config_rejects_invalid_signal_id);
    RUN_TEST(test_apply_config_clamps_brightness_and_panel_count);
    RUN_TEST(test_apply_config_clamps_thresholds_to_effective_range);
    RUN_TEST(test_apply_config_swaps_reversed_thresholds);
    RUN_TEST(test_apply_config_keeps_lone_threshold_on_its_side);
    RUN_TEST(test_preview_changes_live_config_without_saving);
    RUN_TEST(test_apply_during_preview_saves_and_keeps_session);
    RUN_TEST(test_preview_values_replace_live_ones_for_the_session);
    RUN_TEST(test_revert_restores_config_from_before_first_preview);
    RUN_TEST(test_rejected_preview_leaves_live_config_untouched);
    RUN_TEST(test_link_dispatches_request_and_writes_response);
    RUN_TEST(test_ap_controller_activate_sends_command_and_reaches_active);
    RUN_TEST(test_ap_controller_idle_timeout_disables);
    RUN_TEST(test_ap_controller_activity_restarts_idle_timeout);
    RUN_TEST(test_ap_controller_counts_stations_only_while_active);
    RUN_TEST(test_ap_controller_activate_timeout_falls_back_to_idle);
    RUN_TEST(test_ap_controller_reactivate_from_deactivating_resends_enable);
    RUN_TEST(test_ota_begin_round_trips);
    RUN_TEST(test_ota_controller_request_then_upload_then_install);
    RUN_TEST(test_ota_controller_upload_needs_update_mode);
    RUN_TEST(test_ota_controller_decline_and_request_timeout);
    RUN_TEST(test_ota_controller_repeat_request_while_armed_rearms);
    RUN_TEST(test_ota_controller_failed_upload_waits_for_another);
    RUN_TEST(test_ota_controller_leave_ends_update_mode);
    RUN_TEST(test_ota_controller_failed_commit_waits_for_another);
    RUN_TEST(test_ota_controller_timeouts_fall_back);
    RUN_TEST(test_ota_controller_update_mode_gives_up_waiting);
    return UNITY_END();
}
