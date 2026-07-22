#include "remote_config_server.hpp"

#include <string.h>

#include "signal_catalog.hpp"

namespace remote::server
{
    using protocol::ApplyResult;
    using protocol::MsgType;
    using protocol::PanelWire;
    using protocol::SettingsWire;
    using protocol::SignalCatalogEntryWire;
    using protocol::SignalConfigWire;

    namespace
    {
        FLASHMEM void clamp_swap(float &lo, float &hi, float min, float max)
        {
            if (lo < min)
            {
                lo = min;
            }
            if (lo > max)
            {
                lo = max;
            }
            if (hi < min)
            {
                hi = min;
            }
            if (hi > max)
            {
                hi = max;
            }
            if (lo > hi)
            {
                float tmp = lo;
                lo        = hi;
                hi        = tmp;
            }
        }

        FLASHMEM uint8_t clamp_u8(uint32_t v, uint8_t lo, uint8_t hi)
        {
            if (v < lo)
            {
                return lo;
            }
            if (v > hi)
            {
                return hi;
            }
            return static_cast<uint8_t>(v);
        }
    } // namespace

    FLASHMEM RemoteConfigServer::RemoteConfigServer(core::ConfigStore &config,
                                                    core::SignalStore &signals)
        : config_(config)
        , signals_(signals)
    {
    }

    FLASHMEM size_t
    RemoteConfigServer::handle_request(MsgType        req_type,
                                       const uint8_t *req_payload,
                                       uint16_t       req_len,
                                       uint8_t       *resp_buf,
                                       size_t         resp_cap,
                                       MsgType       &resp_type)
    {
        switch (req_type)
        {
            case MsgType::REQ_GET_STATE:
                resp_type = MsgType::RESP_STATE;
                return handle_get_state(resp_buf, resp_cap);

            case MsgType::REQ_APPLY_CONFIG:
                return handle_apply_config(
                    req_payload, req_len, resp_buf, resp_cap, resp_type);

            default:
                resp_type = MsgType::RESP_ERROR;
                return 0;
        }
    }

    FLASHMEM size_t RemoteConfigServer::handle_get_state(uint8_t *out,
                                                         size_t   cap)
    {
        const core::Config &cfg = config_.get();
        uint8_t            *p   = out;
        uint8_t            *end = out + cap;

        SettingsWire settings {};
        settings.day_brightness   = cfg.settings.day_brightness;
        settings.night_brightness = cfg.settings.night_brightness;
        settings.num_panels       = cfg.settings.num_panels;
        settings.orientation
            = cfg.settings.orientation == core::LayoutOrientation::Vertical ? 1
                                                                            : 0;
        settings.accent_color = cfg.settings.accent_color;
        p                     = protocol::encode_settings(p, settings);

        p = protocol::put_u8(p, core::MAX_PANELS);
        for (uint8_t i = 0; i < core::MAX_PANELS; i++)
        {
            PanelWire pw {};
            pw.signal_id = cfg.priorities[i].signal_id;
            pw.units
                = cfg.priorities[i].units == core::UnitSystem::Imperial ? 1 : 0;
            pw.display_mode
                = static_cast<uint8_t>(cfg.priorities[i].display_mode);
            p = protocol::encode_panel(p, pw);
        }

        p = protocol::put_u16(
            p, static_cast<uint16_t>(core::SIGNAL_CATALOG_COUNT));
        for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
        {
            const core::SignalSpec &spec = core::SIGNAL_CATALOG[i];

            SignalCatalogEntryWire entry {};
            entry.id = spec.id;
            strncpy(entry.name, spec.name, protocol::WIRE_NAME_LEN - 1);
            entry.name[protocol::WIRE_NAME_LEN - 1] = '\0';
            entry.unit_type   = static_cast<uint8_t>(spec.unit_type);
            entry.display_min = spec.display_min;
            entry.display_max = spec.display_max;
            p                 = protocol::encode_signal_catalog_entry(p, entry);
        }

        for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
        {
            const core::SignalConfig &sig_cfg = cfg.signals[i];

            SignalConfigWire sw {};
            sw.lo               = sig_cfg.warn.lo;
            sw.hi               = sig_cfg.warn.hi;
            sw.use_lo           = sig_cfg.warn.use_lo ? 1 : 0;
            sw.use_hi           = sig_cfg.warn.use_hi ? 1 : 0;
            sw.use_custom_range = sig_cfg.use_custom_range ? 1 : 0;
            sw.custom_min       = sig_cfg.custom_min;
            sw.custom_max       = sig_cfg.custom_max;
            p                   = protocol::encode_signal_config(p, sw);
        }

        (void)end; /* individual field sizes are fixed/known; a real
                    * overflow here would mean FRAME_MAX_PAYLOAD itself is
                    * too small, not a runtime data-dependent overrun */
        return static_cast<size_t>(p - out);
    }

    FLASHMEM size_t
    RemoteConfigServer::handle_apply_config(const uint8_t *payload,
                                            uint16_t       len,
                                            uint8_t       *out,
                                            size_t         cap,
                                            MsgType       &resp_type)
    {
        (void)len;
        resp_type = MsgType::RESP_APPLY_RESULT;

        const uint8_t *p = payload;

        SettingsWire settings {};
        p = protocol::decode_settings(p, settings);

        PanelWire priorities[core::MAX_PANELS];
        for (uint8_t i = 0; i < core::MAX_PANELS; i++)
        {
            p = protocol::decode_panel(p, priorities[i]);
        }

        uint16_t signal_count = 0;
        p                     = protocol::get_u16(p, signal_count);

        auto fail = [&](ApplyResult result) -> size_t {
            uint8_t *w = out;
            w          = protocol::put_u8(w, static_cast<uint8_t>(result));
            return static_cast<size_t>(w - out);
        };

        if (signal_count != core::SIGNAL_CATALOG_COUNT)
        {
            return fail(ApplyResult::ErrSignalCountMismatch);
        }

        /* Validate the whole batch before mutating anything — a rejected
         * Save should never leave the config half-applied. */
        for (uint8_t i = 0; i < core::MAX_PANELS; i++)
        {
            if (core::signal_catalog_find(priorities[i].signal_id) < 0)
            {
                return fail(ApplyResult::ErrInvalidSignal);
            }
            if (priorities[i].units > 1)
            {
                return fail(ApplyResult::ErrInvalidEnum);
            }
            if (priorities[i].display_mode
                > static_cast<uint8_t>(core::DisplayMode::Gauge))
            {
                return fail(ApplyResult::ErrInvalidEnum);
            }
        }

        SignalConfigWire signals[core::SIGNAL_CATALOG_COUNT];
        for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
        {
            p = protocol::decode_signal_config(p, signals[i]);
        }

        /* Every check above passed — commit. Per signal, resolve the
         * (possibly just-submitted) custom range first, then clamp that
         * signal's thresholds against it, so a range and its thresholds
         * changing in the same batch are bounded consistently rather than
         * against the range that's about to be replaced. */
        core::Config &cfg = config_.get();

        cfg.settings.day_brightness = clamp_u8(settings.day_brightness, 0, 100);
        cfg.settings.night_brightness
            = clamp_u8(settings.night_brightness, 0, 100);
        cfg.settings.num_panels
            = clamp_u8(settings.num_panels, 1, core::MAX_PANELS);
        cfg.settings.orientation  = settings.orientation != 0
                                        ? core::LayoutOrientation::Vertical
                                        : core::LayoutOrientation::Horizontal;
        cfg.settings.accent_color = settings.accent_color;

        for (uint8_t i = 0; i < core::MAX_PANELS; i++)
        {
            cfg.priorities[i].signal_id = priorities[i].signal_id;
            cfg.priorities[i].units     = priorities[i].units != 0
                                              ? core::UnitSystem::Imperial
                                              : core::UnitSystem::Metric;
            cfg.priorities[i].display_mode
                = static_cast<core::DisplayMode>(priorities[i].display_mode);
        }

        for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
        {
            const core::SignalSpec &spec    = core::SIGNAL_CATALOG[i];
            const SignalConfigWire &sw      = signals[i];
            core::SignalConfig     &sig_cfg = cfg.signals[i];

            float custom_min = sw.custom_min;
            float custom_max = sw.custom_max;
            if (sw.use_custom_range != 0 && custom_min < custom_max)
            {
                sig_cfg.use_custom_range = true;
                sig_cfg.custom_min       = custom_min;
                sig_cfg.custom_max       = custom_max;
            }
            else
            {
                sig_cfg.use_custom_range = false;
                sig_cfg.custom_min       = 0.0f;
                sig_cfg.custom_max       = 0.0f;
            }

            core::DisplayRange range
                = core::resolve_display_range(spec,
                                              sig_cfg.use_custom_range,
                                              sig_cfg.custom_min,
                                              sig_cfg.custom_max);

            float lo = sw.lo;
            float hi = sw.hi;
            clamp_swap(lo, hi, range.min, range.max);

            sig_cfg.warn.lo     = lo;
            sig_cfg.warn.hi     = hi;
            sig_cfg.warn.use_lo = sw.use_lo != 0;
            sig_cfg.warn.use_hi = sw.use_hi != 0;
        }

        config_.save();

        (void)cap;
        return fail(ApplyResult::Ok);
    }

} // namespace remote::server
