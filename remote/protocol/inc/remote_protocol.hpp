/**
 * @file remote_protocol.hpp
 * @brief Wire format for the Teensy<->ESP32-C3 link: message types, framing
 * (sync/length/CRC), and per-record encode/decode for every payload shape.
 * Zero core::/gui::/LVGL deps so this compiles verbatim into both
 * firmwares — the wire format can't drift between them. Every multi-byte
 * value is hand-packed little-endian via the put/get helpers below,
 * never memcpy'd from a struct — the two firmwares are different
 * toolchains (arm-none-eabi vs riscv32-esp) with no guaranteed-identical
 * struct padding/enum sizing, so nothing here relies on either.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * This link runs rarely (a batch config load/save, an AP enable/disable)
 * and never on a hot path, so every function and large buffer in remote/
 * carries FLASHMEM/DMAMEM as a matter of course -- see
 * platform/mem_placement.hpp for what they do and when not to use them.
 */
#include "mem_placement.hpp"

namespace remote::protocol
{
    /*==========================================================================
     * BYTE-LEVEL PUT/GET HELPERS (little-endian)
     =========================================================================*/

    uint8_t *put_u8(uint8_t *p, uint8_t v);
    uint8_t *put_u16(uint8_t *p, uint16_t v);
    uint8_t *put_u32(uint8_t *p, uint32_t v);
    uint8_t *put_f32(uint8_t *p, float v);
    uint8_t *put_bytes(uint8_t *p, const void *src, size_t n);

    const uint8_t *get_u8(const uint8_t *p, uint8_t &out);
    const uint8_t *get_u16(const uint8_t *p, uint16_t &out);
    const uint8_t *get_u32(const uint8_t *p, uint32_t &out);
    const uint8_t *get_f32(const uint8_t *p, float &out);
    const uint8_t *get_bytes(const uint8_t *p, void *dst, size_t n);

    /*==========================================================================
     * FRAMING
     =========================================================================*/

    constexpr uint8_t FRAME_SYNC = 0xA5;
    /**
     * RESP_STATE is the largest payload: 58 bytes of catalog entry and
     * config per active signal on top of a 39-byte head and up to 35 bytes
     * of log profile and flags, so 3786 bytes for the 64 signals
     * core::SIGNAL_CONFIG_SLOTS allows.
     * remote_config_server.cpp static_asserts that it fits.
     */
    constexpr uint16_t FRAME_MAX_PAYLOAD = 4096;
    /**
     * A whole frame must arrive within this. ~3x a full 64-signal
     * RESP_STATE (~330 ms) at the C3 link's 115200 baud.
     */
    constexpr uint32_t FRAME_TIMEOUT_MS = 1000;

    constexpr uint8_t WIRE_NAME_LEN = 24; /* matches gui::PANEL_NAME_LEN */
    /**
     * A firmware version string plus NUL -- matches the dash's update
     * descriptor (platform/dash_s3/inc/dash_s3_firmware.hpp).
     */
    constexpr uint8_t WIRE_VERSION_LEN = 32;

    /**
     * REQ_REVERT_PREVIEW's optional payload byte.
     */
    constexpr uint8_t REVERT_PAGE_LEFT = 1;

    enum class MsgType : uint8_t
    {
        REQ_GET_STATE     = 0x01, /* C3 -> Teensy */
        RESP_STATE        = 0x02, /* Teensy -> C3 */
        REQ_APPLY_CONFIG  = 0x03, /* C3 -> Teensy */
        RESP_APPLY_RESULT = 0x04, /* Teensy -> C3 */
        RESP_ERROR
        = 0x05, /* Teensy -> C3, fallback for malformed/unknown REQ_* */
        /**
         * REQ_APPLY_CONFIG's payload, then a u16 count and that many f32
         * values (metric, one per catalog signal, NaN = no value) for the
         * panel to show in place of live ones. Applied to the live config
         * but never saved. The first one starts a preview session and
         * snapshots the saved config; REQ_APPLY_CONFIG saves whatever is
         * live without ending the session, and REQ_REVERT_PREVIEW (or the
         * AP going down) restores the last save and ends it. Both answer
         * with RESP_APPLY_RESULT. RESP_STATE's trailing byte reports the
         * session: bit 0 = active, bit 1 = unsaved changes.
         *
         * Both may end with the log profile: a u8 count (at most
         * core::MAX_LOG_SIGNALS) and that many u16 signal ids. It comes
         * after everything else, the preview values included, so a body
         * without it (a page loaded before it existed) still parses and
         * leaves the profile as it was. RESP_STATE carries the same
         * block, preceded by a u8 core::LogSource, just before its
         * trailing byte.
         */
        REQ_PREVIEW_CONFIG = 0x06, /* C3 -> Teensy */
        /**
         * C3 -> Teensy. Empty: the page's Discard. One byte,
         * REVERT_PAGE_LEFT: the page is going away (closed, switched
         * from, phone locked), so the panel goes back to the QR code
         * rather than staying on the dashboard.
         */
        REQ_REVERT_PREVIEW = 0x07,

        CMD_AP_ENABLE   = 0x10, /* Teensy -> C3 */
        EVT_AP_READY    = 0x11, /* C3 -> Teensy, async */
        CMD_AP_DISABLE  = 0x12, /* Teensy -> C3 */
        EVT_AP_DISABLED = 0x13, /* C3 -> Teensy, best-effort ack */
        /**
         * C3 -> Teensy, u8: how many phones are joined to the AP, sent
         * whenever that changes while it's up. A phone that walks out of
         * range counts until the AP deauths it (RelayApp's inactive
         * time); one that leaves the network drops out at once.
         */
        EVT_AP_STATIONS = 0x14,

        /**
         * Firmware update -- the ESP32-S3 dash only, whose relay writes
         * an uploaded image into the inactive app slot itself (see
         * platform/dash_s3/OTA_PLAN.md). These keep the panel informed,
         * and carry the two things the relay must not decide alone:
         * someone in the car agreeing to update mode, and tapping
         * Install. The relay refuses uploads outside update mode.
         * EVT_OTA_BEGIN goes out only once an upload has passed its
         * identity check, so a rejected file never reaches the panel;
         * every BEGIN ends in an EVT_OTA_RESULT.
         */
        EVT_OTA_BEGIN    = 0x20, /* relay -> dash: OtaBeginWire */
        EVT_OTA_PROGRESS = 0x21, /* relay -> dash: u32 bytes written */
        EVT_OTA_RESULT   = 0x22, /* relay -> dash: u8 OtaResult */
        /**
         * dash -> relay, empty: boot the image the last EVT_OTA_RESULT
         * Ok reported. The relay restarts the board; a failure to switch
         * slots comes back as another EVT_OTA_RESULT.
         */
        CMD_OTA_COMMIT = 0x23,
        /**
         * Update mode. The phone asks (EVT_OTA_REQUEST, empty); the
         * panel answers CMD_OTA_ARM (empty) once someone taps Start, or
         * CMD_OTA_DISARM (empty) if they decline. DISARM also ends update
         * mode later on, and drops any image still awaiting a commit.
         */
        EVT_OTA_REQUEST = 0x24, /* relay -> dash */
        CMD_OTA_ARM     = 0x25, /* dash -> relay */
        CMD_OTA_DISARM  = 0x26, /* dash -> relay */
    };

    enum class ApplyResult : uint8_t
    {
        Ok                     = 0,
        ErrSignalCountMismatch = 1,
        ErrInvalidSignal       = 2,
        ErrInvalidEnum         = 3,
        ErrTruncated           = 4,
    };

    enum class OtaResult : uint8_t
    {
        Ok               = 0, /* written and verified, awaiting a commit */
        ErrNotFirmware   = 1, /* not a firmware image for this dash */
        ErrWrongHardware = 2, /* a dash image built for other hardware */
        ErrTooLarge      = 3, /* bigger than an app slot */
        ErrFlash         = 4, /* a flash write or slot switch failed */
        ErrCorrupt       = 5, /* failed the image's checksum/SHA-256 */
        ErrInterrupted   = 6, /* the upload stopped partway */
        ErrNotArmed      = 7, /* update mode wasn't agreed to on the panel */
        ErrNeedsUsb      = 8, /* built for another partition table */
    };

    /**
     * Standard CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflect,
     * no post-XOR. Table-driven (built once at static-init) — not for
     * speed (frames here are rare), but because a lookup table is
     * trivially identical across compilers, unlike a hand-rolled bit
     * loop. Computed over everything after SYNC and before the CRC
     * itself (MSG_TYPE || LEN || PAYLOAD).
     */
    uint16_t crc16_ccitt(const uint8_t *data, size_t len);

    /**
     * Folds `len` more bytes into a running CRC — lets a CRC be computed
     * over non-contiguous spans (a header and a separately-buffered
     * payload, say) by chaining calls. crc16_ccitt(data, len) is exactly
     * crc16_ccitt_continue(0xFFFF, data, len).
     */
    uint16_t crc16_ccitt_continue(uint16_t       crc,
                                  const uint8_t *data,
                                  size_t         len);

    /**
     * Wraps `payload` into a full SYNC/TYPE/LEN/PAYLOAD/CRC frame.
     *
     * @returns Bytes written to `out`, or 0 if `out_cap` is too small or
     * `payload_len` exceeds FRAME_MAX_PAYLOAD.
     */
    size_t encode_frame(MsgType        type,
                        const uint8_t *payload,
                        uint16_t       payload_len,
                        uint8_t       *out,
                        size_t         out_cap);

    struct DecodedFrame
    {
            MsgType        type        = MsgType::RESP_ERROR;
            const uint8_t *payload     = nullptr;
            uint16_t       payload_len = 0;
    };

    /**
     * Non-blocking, byte-at-a-time frame assembler — feed it one received
     * byte per call so it can run from a main loop without blocking, the
     * same shape core::can::UdsClient::poll() already has for CAN frames.
     * Self-resyncs on a stray non-sync byte, drops a frame whose CRC
     * doesn't match, and drops a frame that stalls mid-receive past
     * FRAME_TIMEOUT_MS — none of those can wedge the reader.
     */
    class FrameReader
    {
        public:
            /**
             * Feeds one received byte.
             *
             * @returns True exactly on the byte that completes a
             * CRC-valid frame; decoded_frame() is valid until the next
             * on_byte() call (its `payload` pointer aliases this
             * instance's internal buffer).
             */
            bool on_byte(uint8_t b, uint32_t now_ms);

            const DecodedFrame &decoded_frame() const
            {
                return frame_;
            }

        private:
            enum class State : uint8_t
            {
                WaitSync,
                Type,
                LenLo,
                LenHi,
                Payload,
                CrcLo,
                CrcHi
            };

            void reset();

            /**
             * Where payload bytes go; null only if the ESP32 build's
             * allocation failed.
             */
            uint8_t *payload_buf();

            State    state_          = State::WaitSync;
            uint8_t  msg_type_       = 0;
            uint16_t len_            = 0;
            uint16_t received_       = 0;
            uint16_t crc_received_   = 0;
            uint32_t frame_start_ms_ = 0;
#if defined(ARDUINO_ARCH_ESP32)
            /**
             * One per reader, in PSRAM on the S3 (see LazyBuffer). The S3
             * has two readers alive at once, the dash's and the in-process
             * relay's, on different cores, so they can't share one.
             */
            LazyBuffer<uint8_t, FRAME_MAX_PAYLOAD> buf_;
#else
            /**
             * static, not a plain member: GCC rejects a `section`
             * attribute on a non-static data member (there's no
             * independent storage to place — it's part of whatever object
             * contains it), so DMAMEM placement needs static storage
             * duration; see the out-of-class definition in
             * remote_protocol.cpp. Safe because there's only ever one
             * FrameReader alive on the Teensy (inside RemoteConfigLink,
             * inside the Composition singleton); tests construct more
             * than one, but Unity runs test cases sequentially, never two
             * FrameReaders mid-frame at once. Never read before being
             * written (bounded by payload_len), so it doesn't need — and
             * DMAMEM's .bss.dma section doesn't guarantee — zero-
             * initialization at startup.
             */
            static uint8_t buf_[FRAME_MAX_PAYLOAD];
#endif
            DecodedFrame   frame_ {};
    };

    /*==========================================================================
     * PER-RECORD WIRE STRUCTS
     *
     * Plain in-memory records (not wire-laid-out — see the file doc
     * comment) built/consumed one at a time by remote/server, looping
     * over MAX_PANELS/SIGNAL_CATALOG_COUNT itself rather than this layer
     * holding a fixed-size array of them.
     =========================================================================*/

    struct SettingsWire
    {
            uint8_t  day_brightness   = 0;
            uint8_t  night_brightness = 0;
            uint8_t  num_panels       = 0;
            uint8_t  orientation      = 0; /* 0=Horizontal, 1=Vertical */
            uint32_t accent_color     = 0;
            /**
             * Bit 0: the horizontal layout turned 180 degrees, bit 1: the
             * vertical one (Settings::flipped_horizontal/_vertical).
             */
            uint8_t  flips            = 0;
            /**
             * core::BrightnessMode: 0=Day, 1=Night, 2=Auto.
             */
            uint8_t  brightness_mode  = 0;
            /**
             * Settings::auto_brightness_pct, 0-100 (50 = no shift).
             */
            uint8_t  auto_brightness  = 0;
    };
    uint8_t       *encode_settings(uint8_t *p, const SettingsWire &v);
    const uint8_t *decode_settings(const uint8_t *p, SettingsWire &out);

    struct PanelWire
    {
            uint16_t signal_id    = 0;
            uint8_t  units        = 0; /* 0=Metric, 1=Imperial */
            uint8_t  display_mode = 0; /* 0=Digital, 1=Bar, 2=Gauge */
    };
    uint8_t       *encode_panel(uint8_t *p, const PanelWire &v);
    const uint8_t *decode_panel(const uint8_t *p, PanelWire &out);

    struct SignalCatalogEntryWire
    {
            uint16_t id                  = 0;
            char     name[WIRE_NAME_LEN] = "";
            uint8_t  unit_type           = 0;
            float    display_min         = 0.0f;
            float    display_max         = 0.0f;
            /**
             * SignalSpec::cycle_time_ms: how often the dash asks for it,
             * which is the fastest it can log.
             */
            uint16_t cycle_time_ms       = 0;
            /**
             * Request CAN id of the module its first request goes to
             * (0x7E0 engine, ...), for grouping on the phone page.
             */
            uint16_t module_tx           = 0;
    };
    uint8_t       *encode_signal_catalog_entry(uint8_t                      *p,
                                               const SignalCatalogEntryWire &v);
    const uint8_t *decode_signal_catalog_entry(const uint8_t          *p,
                                               SignalCatalogEntryWire &out);

    struct SignalConfigWire
    {
            float   lo               = 0.0f;
            float   hi               = 0.0f;
            uint8_t use_lo           = 0;
            uint8_t use_hi           = 0;
            uint8_t use_custom_range = 0;
            float   custom_min       = 0.0f;
            float   custom_max       = 0.0f;
    };
    uint8_t       *encode_signal_config(uint8_t *p, const SignalConfigWire &v);
    const uint8_t *decode_signal_config(const uint8_t    *p,
                                        SignalConfigWire &out);

    /**
     * WPA2 max sizes + NUL. Generated fresh in RAM by the Teensy on every
     * Wireless Setup activation, never persisted.
     */
    struct ApEnableWire
    {
            char ssid[33]     = "";
            char password[65] = "";
    };
    uint8_t       *encode_ap_enable(uint8_t *p, const ApEnableWire &v);
    const uint8_t *decode_ap_enable(const uint8_t *p, ApEnableWire &out);

    struct ApReadyWire
    {
            uint8_t ip[4] = { 0, 0, 0, 0 };
    };
    uint8_t       *encode_ap_ready(uint8_t *p, const ApReadyWire &v);
    const uint8_t *decode_ap_ready(const uint8_t *p, ApReadyWire &out);

    struct OtaBeginWire
    {
            uint32_t total_bytes                = 0;
            char     version[WIRE_VERSION_LEN] = "";
    };
    constexpr uint16_t OTA_BEGIN_BYTES = 4 + WIRE_VERSION_LEN;
    uint8_t       *encode_ota_begin(uint8_t *p, const OtaBeginWire &v);
    const uint8_t *decode_ota_begin(const uint8_t *p, OtaBeginWire &out);

} // namespace remote::protocol
