#include "remote_protocol.hpp"

#include <string.h>

namespace remote::protocol
{
    /**
     * ==========================================================================
     * PUT/GET
     =========================================================================*/

    FLASHMEM uint8_t *put_u8(uint8_t *p, uint8_t v)
    {
        *p = v;
        return p + 1;
    }

    FLASHMEM uint8_t *put_u16(uint8_t *p, uint16_t v)
    {
        p[0] = static_cast<uint8_t>(v & 0xFF);
        p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        return p + 2;
    }

    FLASHMEM uint8_t *put_u32(uint8_t *p, uint32_t v)
    {
        p[0] = static_cast<uint8_t>(v & 0xFF);
        p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
        return p + 4;
    }

    FLASHMEM uint8_t *put_f32(uint8_t *p, float v)
    {
        uint32_t bits;
        memcpy(&bits, &v, sizeof(bits));
        return put_u32(p, bits);
    }

    FLASHMEM uint8_t *put_bytes(uint8_t *p, const void *src, size_t n)
    {
        memcpy(p, src, n);
        return p + n;
    }

    FLASHMEM const uint8_t *get_u8(const uint8_t *p, uint8_t &out)
    {
        out = *p;
        return p + 1;
    }

    FLASHMEM const uint8_t *get_u16(const uint8_t *p, uint16_t &out)
    {
        out = static_cast<uint16_t>(p[0] | (p[1] << 8));
        return p + 2;
    }

    FLASHMEM const uint8_t *get_u32(const uint8_t *p, uint32_t &out)
    {
        out = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
              | (static_cast<uint32_t>(p[2]) << 16)
              | (static_cast<uint32_t>(p[3]) << 24);
        return p + 4;
    }

    FLASHMEM const uint8_t *get_f32(const uint8_t *p, float &out)
    {
        uint32_t bits;
        p = get_u32(p, bits);
        memcpy(&out, &bits, sizeof(out));
        return p;
    }

    FLASHMEM const uint8_t *get_bytes(const uint8_t *p, void *dst, size_t n)
    {
        memcpy(dst, p, n);
        return p + n;
    }

    /**
     * ==========================================================================
     * CRC-16/CCITT-FALSE
     =========================================================================*/

    namespace
    {
        constexpr uint16_t CRC16_POLY = 0x1021;
    } // namespace

    /**
     * Bitwise rather than the usual 256-entry lookup table. The table
     * would be 512 bytes of RAM (a static constructor's worth of boot
     * work to fill, since the CRC polynomial loop isn't foldable at
     * compile time under this file's oldest target standard) or 512 bytes
     * of flash, to speed up a path that is explicitly not hot: this link
     * carries a batch config load/save and an AP enable/disable, nothing
     * per-frame -- which is exactly why every function in this file is
     * FLASHMEM. Eight shifts per byte on a 600MHz M7 is microseconds
     * across a whole maximum-sized frame. */
    FLASHMEM uint16_t crc16_ccitt_continue(uint16_t       crc,
                                           const uint8_t *data,
                                           size_t         len)
    {
        for (size_t i = 0; i < len; i++)
        {
            crc = static_cast<uint16_t>(crc ^ (data[i] << 8));
            for (uint8_t bit = 0; bit < 8; bit++)
            {
                crc = (crc & 0x8000)
                          ? static_cast<uint16_t>((crc << 1) ^ CRC16_POLY)
                          : static_cast<uint16_t>(crc << 1);
            }
        }
        return crc;
    }

    FLASHMEM uint16_t crc16_ccitt(const uint8_t *data, size_t len)
    {
        return crc16_ccitt_continue(0xFFFF, data, len);
    }

    /**
     * ==========================================================================
     * FRAMING
     =========================================================================*/

    DMAMEM uint8_t FrameReader::buf_[FRAME_MAX_PAYLOAD];

    FLASHMEM size_t encode_frame(MsgType        type,
                                 const uint8_t *payload,
                                 uint16_t       payload_len,
                                 uint8_t       *out,
                                 size_t         out_cap)
    {
        size_t total
            = 1 + 1 + 2 + payload_len + 2; /* sync+type+len+payload+crc */
        if (payload_len > FRAME_MAX_PAYLOAD || total > out_cap)
        {
            return 0;
        }

        uint8_t *p         = out;
        uint8_t *crc_start = p + 1; /* everything after SYNC */

        p = put_u8(p, FRAME_SYNC);
        p = put_u8(p, static_cast<uint8_t>(type));
        p = put_u16(p, payload_len);
        if (payload_len > 0)
        {
            p = put_bytes(p, payload, payload_len);
        }

        uint16_t crc
            = crc16_ccitt(crc_start, static_cast<size_t>(p - crc_start));
        p = put_u16(p, crc);

        return static_cast<size_t>(p - out);
    }

    FLASHMEM void FrameReader::reset()
    {
        state_    = State::WaitSync;
        len_      = 0;
        received_ = 0;
    }

    FLASHMEM bool FrameReader::on_byte(uint8_t b, uint32_t now_ms)
    {
        if (state_ != State::WaitSync
            && (now_ms - frame_start_ms_) > FRAME_TIMEOUT_MS)
        {
            reset();
        }

        switch (state_)
        {
            case State::WaitSync:
                if (b == FRAME_SYNC)
                {
                    frame_start_ms_ = now_ms;
                    state_          = State::Type;
                }
                return false;

            case State::Type:
                msg_type_ = b;
                state_    = State::LenLo;
                return false;

            case State::LenLo:
                len_   = b;
                state_ = State::LenHi;
                return false;

            case State::LenHi:
                len_ = static_cast<uint16_t>(len_ | (b << 8));
                if (len_ > FRAME_MAX_PAYLOAD)
                {
                    reset();
                    return false;
                }
                received_ = 0;
                state_    = (len_ == 0) ? State::CrcLo : State::Payload;
                return false;

            case State::Payload:
                buf_[received_++] = b;
                if (received_ == len_)
                {
                    state_ = State::CrcLo;
                }
                return false;

            case State::CrcLo:
                crc_received_ = b;
                state_        = State::CrcHi;
                return false;

            case State::CrcHi: {
                crc_received_ = static_cast<uint16_t>(crc_received_ | (b << 8));

                /**
                 * CRC covers MSG_TYPE || LEN || PAYLOAD — header and
                 * payload aren't contiguous in memory here, so the CRC is
                 * chained across two calls via crc16_ccitt_update(). */
                uint8_t header[3]
                    = { msg_type_,
                        static_cast<uint8_t>(len_ & 0xFF),
                        static_cast<uint8_t>((len_ >> 8) & 0xFF) };
                uint16_t crc
                    = crc16_ccitt_continue(0xFFFF, header, sizeof(header));
                crc = crc16_ccitt_continue(crc, buf_, len_);

                bool     ok          = (crc == crc_received_);
                uint8_t  type        = msg_type_;
                uint16_t payload_len = len_;

                reset(); /**
                          * clears len_/received_ — must run after both are
                          * captured above, not before */

                if (!ok)
                {
                    return false;
                }

                frame_.type        = static_cast<MsgType>(type);
                frame_.payload     = buf_;
                frame_.payload_len = payload_len;
                return true;
            }
        }

        return false;
    }

    /**
     * ==========================================================================
     * PER-RECORD WIRE STRUCTS
     =========================================================================*/

    FLASHMEM uint8_t *encode_settings(uint8_t *p, const SettingsWire &v)
    {
        p = put_u8(p, v.day_brightness);
        p = put_u8(p, v.night_brightness);
        p = put_u8(p, v.num_panels);
        p = put_u8(p, v.orientation);
        p = put_u32(p, v.accent_color);
        return p;
    }

    FLASHMEM const uint8_t *decode_settings(const uint8_t *p, SettingsWire &out)
    {
        p = get_u8(p, out.day_brightness);
        p = get_u8(p, out.night_brightness);
        p = get_u8(p, out.num_panels);
        p = get_u8(p, out.orientation);
        p = get_u32(p, out.accent_color);
        return p;
    }

    FLASHMEM uint8_t *encode_panel(uint8_t *p, const PanelWire &v)
    {
        p = put_u16(p, v.signal_id);
        p = put_u8(p, v.units);
        p = put_u8(p, v.display_mode);
        return p;
    }

    FLASHMEM const uint8_t *decode_panel(const uint8_t *p, PanelWire &out)
    {
        p = get_u16(p, out.signal_id);
        p = get_u8(p, out.units);
        p = get_u8(p, out.display_mode);
        return p;
    }

    FLASHMEM uint8_t *encode_signal_catalog_entry(
        uint8_t *p, const SignalCatalogEntryWire &v)
    {
        p = put_u16(p, v.id);
        p = put_bytes(p, v.name, WIRE_NAME_LEN);
        p = put_u8(p, v.unit_type);
        p = put_f32(p, v.display_min);
        p = put_f32(p, v.display_max);
        return p;
    }

    FLASHMEM const uint8_t *decode_signal_catalog_entry(
        const uint8_t *p, SignalCatalogEntryWire &out)
    {
        p = get_u16(p, out.id);
        p = get_bytes(p, out.name, WIRE_NAME_LEN);
        p = get_u8(p, out.unit_type);
        p = get_f32(p, out.display_min);
        p = get_f32(p, out.display_max);
        return p;
    }

    FLASHMEM uint8_t *encode_signal_config(uint8_t                *p,
                                           const SignalConfigWire &v)
    {
        p = put_f32(p, v.lo);
        p = put_f32(p, v.hi);
        p = put_u8(p, v.use_lo);
        p = put_u8(p, v.use_hi);
        p = put_u8(p, v.use_custom_range);
        p = put_f32(p, v.custom_min);
        p = put_f32(p, v.custom_max);
        return p;
    }

    FLASHMEM const uint8_t *decode_signal_config(const uint8_t    *p,
                                                 SignalConfigWire &out)
    {
        p = get_f32(p, out.lo);
        p = get_f32(p, out.hi);
        p = get_u8(p, out.use_lo);
        p = get_u8(p, out.use_hi);
        p = get_u8(p, out.use_custom_range);
        p = get_f32(p, out.custom_min);
        p = get_f32(p, out.custom_max);
        return p;
    }

    FLASHMEM uint8_t *encode_ap_enable(uint8_t *p, const ApEnableWire &v)
    {
        p = put_bytes(p, v.ssid, sizeof(v.ssid));
        p = put_bytes(p, v.password, sizeof(v.password));
        return p;
    }

    FLASHMEM const uint8_t *decode_ap_enable(const uint8_t *p,
                                             ApEnableWire  &out)
    {
        p = get_bytes(p, out.ssid, sizeof(out.ssid));
        p = get_bytes(p, out.password, sizeof(out.password));
        return p;
    }

    FLASHMEM uint8_t *encode_ap_ready(uint8_t *p, const ApReadyWire &v)
    {
        return put_bytes(p, v.ip, sizeof(v.ip));
    }

    FLASHMEM const uint8_t *decode_ap_ready(const uint8_t *p, ApReadyWire &out)
    {
        return get_bytes(p, out.ip, sizeof(out.ip));
    }

} // namespace remote::protocol
