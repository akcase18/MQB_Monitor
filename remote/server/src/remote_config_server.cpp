#include "remote_config_server.hpp"

#include <math.h>
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
        /* Wire sizes of each record -- see the encode_*() functions in
         * remote_protocol.cpp. A config batch is settings, MAX_PANELS
         * panels, the signal count and one SignalConfigWire per signal. */
        constexpr size_t SETTINGS_BYTES = 11;
        constexpr size_t PANEL_BYTES    = 4;
        constexpr size_t SIGNAL_BYTES   = 19;
        constexpr size_t HEAD_LEN
            = SETTINGS_BYTES + core::MAX_PANELS * PANEL_BYTES + 2;
        constexpr size_t BATCH_LEN
            = HEAD_LEN + core::SIGNAL_CATALOG_COUNT * SIGNAL_BYTES;

        /**
         * The optional log profile block: a count, then the ids.
         */
        constexpr size_t LOG_PROFILE_MAX_BYTES = 1 + core::MAX_LOG_SIGNALS * 2;

        /**
         * RESP_STATE: settings, the panel count and panels, the signal
         * count, a catalog entry and a config per signal, the log source
         * and profile, then the flags byte. See handle_get_state(). This
         * is the most it can be: the profile is only as long as it is.
         */
        constexpr size_t CATALOG_ENTRY_BYTES
            = 2 + protocol::WIRE_NAME_LEN + 1 + 4 + 4 + 2 + 2;
        constexpr size_t state_len(size_t signals)
        {
            return SETTINGS_BYTES + 1 + core::MAX_PANELS * PANEL_BYTES + 2
                   + signals * (CATALOG_ENTRY_BYTES + SIGNAL_BYTES) + 1
                   + LOG_PROFILE_MAX_BYTES + 1;
        }

        /**
         * Sized against the most signals the config can ever hold, not
         * today's catalog, so promoting signals can't outgrow the frame.
         */
        static_assert(state_len(core::SIGNAL_CONFIG_SLOTS)
                          <= protocol::FRAME_MAX_PAYLOAD,
                      "RESP_STATE for a full catalog no longer fits a frame "
                      "-- raise FRAME_MAX_PAYLOAD");

        /* Clamps both thresholds into the signal's range, and puts a
         * reversed pair back in order -- but only when both are in use. A
         * disabled threshold keeps whatever value it was last left at,
         * which is routinely on the "wrong" side of the enabled one (a
         * high warning set to 1.6 below an untouched low of 2.0); swapping
         * then would silently move the warning the user actually set. */
        FLASHMEM void clamp_thresholds(float &lo,
                                       float &hi,
                                       bool   use_lo,
                                       bool   use_hi,
                                       float  min,
                                       float  max)
        {
            lo = lo < min ? min : (lo > max ? max : lo);
            hi = hi < min ? min : (hi > max ? max : hi);
            if (use_lo && use_hi && lo > hi)
            {
                float tmp = lo;
                lo        = hi;
                hi        = tmp;
            }
        }

        /* Field by field rather than memcmp, which would also compare
         * struct padding. A threshold or custom range only counts while
         * it's switched on: apply_to_live() clamps every threshold into
         * range whether it's in use or not, so a never-set (0, 0) pair on
         * a signal whose range excludes 0 comes back from a round trip
         * through the phone moved, with nothing actually changed. */
        FLASHMEM bool configs_equal(const core::Config &a, const core::Config &b)
        {
            const core::Settings &sa = a.settings;
            const core::Settings &sb = b.settings;
            if (sa.day_brightness != sb.day_brightness
                || sa.night_brightness != sb.night_brightness
                || sa.num_panels != sb.num_panels
                || sa.orientation != sb.orientation
                || sa.accent_color != sb.accent_color
                || sa.flipped_horizontal != sb.flipped_horizontal
                || sa.flipped_vertical != sb.flipped_vertical
                || sa.brightness_mode != sb.brightness_mode
                || sa.auto_brightness_pct != sb.auto_brightness_pct)
            {
                return false;
            }
            for (uint8_t i = 0; i < core::MAX_PANELS; i++)
            {
                const core::PrioritizedSignal &pa = a.priorities[i];
                const core::PrioritizedSignal &pb = b.priorities[i];
                if (pa.signal_id != pb.signal_id || pa.units != pb.units
                    || pa.display_mode != pb.display_mode)
                {
                    return false;
                }
            }
            for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
            {
                const core::SignalConfig &ca = a.signals[i];
                const core::SignalConfig &cb = b.signals[i];
                if (ca.warn.use_lo != cb.warn.use_lo
                    || ca.warn.use_hi != cb.warn.use_hi
                    || ca.use_custom_range != cb.use_custom_range)
                {
                    return false;
                }
                if ((ca.warn.use_lo && ca.warn.lo != cb.warn.lo)
                    || (ca.warn.use_hi && ca.warn.hi != cb.warn.hi)
                    || (ca.use_custom_range
                        && (ca.custom_min != cb.custom_min
                            || ca.custom_max != cb.custom_max)))
                {
                    return false;
                }
            }
            if (a.log_count != b.log_count)
            {
                return false;
            }
            for (uint8_t i = 0; i < a.log_count && i < core::MAX_LOG_SIGNALS;
                 i++)
            {
                if (a.log_signals[i] != b.log_signals[i])
                {
                    return false;
                }
            }
            return true;
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

#if defined(ARDUINO_ARCH_ESP32)
    core::Config &RemoteConfigServer::saved_before_preview_
        = psram_new<core::Config>();
#else
    DMAMEM core::Config RemoteConfigServer::saved_before_preview_;
#endif

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

            case MsgType::REQ_PREVIEW_CONFIG:
            case MsgType::REQ_APPLY_CONFIG:
            case MsgType::REQ_REVERT_PREVIEW:
                resp_type = MsgType::RESP_APPLY_RESULT;
                if (resp_cap < 1)
                {
                    return 0;
                }
                protocol::put_u8(resp_buf,
                                 static_cast<uint8_t>(handle_config_change(
                                     req_type, req_payload, req_len)));
                return 1;

            default:
                resp_type = MsgType::RESP_ERROR;
                return 0;
        }
    }

    FLASHMEM void RemoteConfigServer::set_config_callback(ConfigCallback cb, void *ctx)
    {
        config_cb_  = cb;
        config_ctx_ = ctx;
    }

    FLASHMEM void RemoteConfigServer::revert_preview()
    {
        end_preview(ConfigEvent::Reverted);
    }

    FLASHMEM void RemoteConfigServer::end_preview(ConfigEvent event)
    {
        if (!preview_active_)
        {
            return;
        }
        config_.get()   = saved_before_preview_;
        preview_active_ = false;
        unsaved_        = false;
        notify(event);
    }

    FLASHMEM void RemoteConfigServer::notify(ConfigEvent event)
    {
        if (config_cb_ != nullptr)
        {
            config_cb_(config_ctx_, event);
        }
    }

    FLASHMEM ApplyResult
    RemoteConfigServer::handle_config_change(MsgType        req_type,
                                             const uint8_t *payload,
                                             uint16_t       len)
    {
        if (req_type == MsgType::REQ_REVERT_PREVIEW)
        {
            bool left = len >= 1 && payload[0] == protocol::REVERT_PAGE_LEFT;
            end_preview(left ? ConfigEvent::PageLeft : ConfigEvent::Reverted);
            return ApplyResult::Ok;
        }

        /* Captured before apply_to_live() touches anything, so it is the
         * saved config even when this first preview is the one rejected
         * below -- a rejection leaves the live config alone, and the
         * snapshot then simply equals it. */
        bool starting_preview
            = req_type == MsgType::REQ_PREVIEW_CONFIG && !preview_active_;
        if (starting_preview)
        {
            saved_before_preview_ = config_.get();
        }

        /**
         * The log profile, if the page sent one, comes after everything
         * else -- past the preview values on a preview, whose own count
         * says how long they are. A body too short to say is left to
         * apply_to_live() to reject.
         */
        size_t log_at = BATCH_LEN;
        if (req_type == MsgType::REQ_PREVIEW_CONFIG && len >= BATCH_LEN + 2)
        {
            uint16_t values = 0;
            protocol::get_u16(payload + BATCH_LEN, values);
            log_at = BATCH_LEN + 2 + static_cast<size_t>(values) * 4;
        }

        ApplyResult result = apply_to_live(payload, len, log_at);
        if (result != ApplyResult::Ok)
        {
            return result;
        }

        if (req_type == MsgType::REQ_PREVIEW_CONFIG)
        {
            read_preview_values(payload + BATCH_LEN, len - BATCH_LEN);
            preview_active_ = true;
            unsaved_ = !configs_equal(config_.get(), saved_before_preview_);
            notify(ConfigEvent::Previewed);
        }
        else
        {
            config_.save();
            /* What a later revert goes back to is now this. */
            saved_before_preview_ = config_.get();
            unsaved_              = false;
            notify(ConfigEvent::Committed);
        }
        return ApplyResult::Ok;
    }

    FLASHMEM void RemoteConfigServer::read_preview_values(const uint8_t *p,
                                                          size_t         len)
    {
        preview_values_ = core::SignalStore {};

        uint16_t count = 0;
        if (len < 2)
        {
            return;
        }
        p = protocol::get_u16(p, count);
        if (count != core::SIGNAL_CATALOG_COUNT || len < 2 + count * 4u)
        {
            return;
        }

        for (size_t i = 0; i < count; i++)
        {
            float value = 0.0f;
            p           = protocol::get_f32(p, value);
            if (!isnan(value))
            {
                preview_values_.set_at(static_cast<int>(i), value, 0);
            }
        }
    }

    FLASHMEM size_t RemoteConfigServer::handle_get_state(uint8_t *out,
                                                         size_t   cap)
    {
        const core::Config &cfg = config_.get();
        uint8_t            *p   = out;
        uint8_t            *end = out + cap;

        /**
         * Every field below has a fixed size, so this one check covers the
         * whole encode. Refusing beats writing past a short buffer.
         */
        if (cap < state_len(core::SIGNAL_CATALOG_COUNT))
        {
            return 0;
        }

        SettingsWire settings {};
        settings.day_brightness   = cfg.settings.day_brightness;
        settings.night_brightness = cfg.settings.night_brightness;
        settings.num_panels       = cfg.settings.num_panels;
        settings.orientation
            = cfg.settings.orientation == core::LayoutOrientation::Vertical ? 1
                                                                            : 0;
        settings.accent_color = cfg.settings.accent_color;
        settings.flips        = static_cast<uint8_t>(
            (cfg.settings.flipped_horizontal ? 1 : 0)
            | (cfg.settings.flipped_vertical ? 2 : 0));
        settings.brightness_mode
            = static_cast<uint8_t>(cfg.settings.brightness_mode);
        settings.auto_brightness = cfg.settings.auto_brightness_pct;
        p = protocol::encode_settings(p, settings);

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
            entry.display_max   = spec.display_max;
            entry.cycle_time_ms = static_cast<uint16_t>(
                spec.cycle_time_ms > 0xFFFF ? 0xFFFF : spec.cycle_time_ms);
            entry.module_tx = spec.num_requests > 0
                                  ? core::UDS_ECUS[spec.requests[0].ecu].tx_id
                                  : 0;
            p = protocol::encode_signal_catalog_entry(p, entry);
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

        uint8_t log_count = cfg.log_count > core::MAX_LOG_SIGNALS
                                ? core::MAX_LOG_SIGNALS
                                : cfg.log_count;
        p = protocol::put_u8(p, static_cast<uint8_t>(cfg.settings.log_source));
        p = protocol::put_u8(p, log_count);
        for (uint8_t i = 0; i < log_count; i++)
        {
            p = protocol::put_u16(p, cfg.log_signals[i]);
        }

        p = protocol::put_u8(
            p,
            static_cast<uint8_t>((preview_active_ ? 1 : 0) | (unsaved_ ? 2 : 0)));

        (void)end; /* checked up front against state_len() */
        return static_cast<size_t>(p - out);
    }

    FLASHMEM ApplyResult RemoteConfigServer::apply_to_live(
        const uint8_t *payload,
        uint16_t       len,
        size_t         log_at)
    {
        /* The batch comes from browser-side JS rather than this
         * firmware's own encoder, so a short one is checked here instead
         * of being decoded off the end of the frame buffer. */
        if (payload == nullptr || len < HEAD_LEN)
        {
            return ApplyResult::ErrTruncated;
        }

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

        if (signal_count != core::SIGNAL_CATALOG_COUNT)
        {
            return ApplyResult::ErrSignalCountMismatch;
        }
        if (len < BATCH_LEN)
        {
            return ApplyResult::ErrTruncated;
        }

        /* Validate the whole batch before mutating anything — a rejected
         * Save should never leave the config half-applied. */
        if (settings.brightness_mode
            > static_cast<uint8_t>(core::BrightnessMode::Auto))
        {
            return ApplyResult::ErrInvalidEnum;
        }
        for (uint8_t i = 0; i < core::MAX_PANELS; i++)
        {
            if (core::signal_catalog_find(priorities[i].signal_id) < 0)
            {
                return ApplyResult::ErrInvalidSignal;
            }
            if (priorities[i].units > 1)
            {
                return ApplyResult::ErrInvalidEnum;
            }
            if (priorities[i].display_mode
                > static_cast<uint8_t>(core::DisplayMode::Gauge))
            {
                return ApplyResult::ErrInvalidEnum;
            }
        }

        SignalConfigWire signals[core::SIGNAL_CATALOG_COUNT];
        for (size_t i = 0; i < core::SIGNAL_CATALOG_COUNT; i++)
        {
            p = protocol::decode_signal_config(p, signals[i]);
        }

        /**
         * The log profile: optional (see REQ_PREVIEW_CONFIG), and held to
         * the same rules ConfigStore::load() would otherwise fix up --
         * every id offered by this build, none twice, at most
         * MAX_LOG_SIGNALS -- so the phone hears about a bad one instead
         * of the dash quietly dropping it.
         */
        bool     has_log = len > log_at;
        uint8_t  log_count = 0;
        uint16_t log_ids[core::MAX_LOG_SIGNALS] = {};
        if (has_log)
        {
            const uint8_t *q = protocol::get_u8(payload + log_at, log_count);
            if (log_count > core::MAX_LOG_SIGNALS)
            {
                return ApplyResult::ErrInvalidSignal;
            }
            if (len < log_at + 1 + static_cast<size_t>(log_count) * 2)
            {
                return ApplyResult::ErrTruncated;
            }
            for (uint8_t i = 0; i < log_count; i++)
            {
                q = protocol::get_u16(q, log_ids[i]);
                if (core::signal_catalog_find(log_ids[i]) < 0)
                {
                    return ApplyResult::ErrInvalidSignal;
                }
                for (uint8_t k = 0; k < i; k++)
                {
                    if (log_ids[k] == log_ids[i])
                    {
                        return ApplyResult::ErrInvalidSignal;
                    }
                }
            }
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
        cfg.settings.flipped_horizontal = (settings.flips & 1) != 0;
        cfg.settings.flipped_vertical   = (settings.flips & 2) != 0;
        cfg.settings.brightness_mode
            = static_cast<core::BrightnessMode>(settings.brightness_mode);
        cfg.settings.auto_brightness_pct
            = clamp_u8(settings.auto_brightness, 0, 100);

        if (has_log)
        {
            cfg.log_count = log_count;
            for (uint8_t i = 0; i < core::MAX_LOG_SIGNALS; i++)
            {
                cfg.log_signals[i] = i < log_count ? log_ids[i]
                                                   : core::NO_SIGNAL_ID;
            }
        }

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
            clamp_thresholds(lo,
                             hi,
                             sw.use_lo != 0,
                             sw.use_hi != 0,
                             range.min,
                             range.max);

            sig_cfg.warn.lo     = lo;
            sig_cfg.warn.hi     = hi;
            sig_cfg.warn.use_lo = sw.use_lo != 0;
            sig_cfg.warn.use_hi = sw.use_hi != 0;
        }

        return ApplyResult::Ok;
    }

} // namespace remote::server
