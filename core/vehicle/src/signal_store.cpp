#include "signal_store.hpp"

namespace core
{

    namespace
    {
        /* Ported from the old Info::round(). */
        float round_half_up(float value, uint8_t decimal_places)
        {
            int32_t scale = 1;
            for (uint8_t i = 0; i < decimal_places; i++)
            {
                scale *= 10;
            }

            return static_cast<float>(
                       static_cast<int32_t>(value * scale + 0.5f))
                   / static_cast<float>(scale);
        }
    } // namespace

    const SignalValue SignalStore::s_invalid {};

    void SignalStore::set(uint16_t id, float value, uint32_t now_ms)
    {
        set_at(signal_catalog_find(id), value, now_ms);
    }

    const SignalValue &SignalStore::get(uint16_t id) const
    {
        return get_at(signal_catalog_find(id));
    }

    float SignalStore::get_display_value(uint16_t id, bool is_metric) const
    {
        return get_display_value_at(signal_catalog_find(id), is_metric);
    }

    void SignalStore::set_at(int idx, float value, uint32_t now_ms)
    {
        if (idx < 0 || idx >= static_cast<int>(SIGNAL_CATALOG_COUNT))
        {
            return;
        }

        SignalValue &v   = values_[idx];
        v.value          = value;
        v.has_data       = true;
        v.last_update_ms = now_ms;
    }

    const SignalValue &SignalStore::get_at(int idx) const
    {
        if (idx < 0 || idx >= static_cast<int>(SIGNAL_CATALOG_COUNT))
        {
            return s_invalid;
        }
        return values_[idx];
    }

    float SignalStore::get_display_value_at(int idx, bool is_metric) const
    {
        if (idx < 0 || idx >= static_cast<int>(SIGNAL_CATALOG_COUNT))
        {
            return 0.0f;
        }

        const SignalSpec &spec  = SIGNAL_CATALOG[idx];
        float             value = values_[idx].value;

        if (!is_metric)
        {
            value = unit_convert_to_imperial(spec.unit_type, value);
        }

        /* Old round_data(): Volume/Pressure round to N decimals, everything
         * else truncates toward zero (no rounding). Preserved as-is. */
        switch (spec.unit_type)
        {
            case UnitType::Volume:
                value = round_half_up(value, 1);
                break;

            case UnitType::Pressure:
                value = round_half_up(value, is_metric ? 2 : 1);
                break;

            default:
                value = static_cast<float>(static_cast<int32_t>(value));
                break;
        }

        return value;
    }

    void SignalStore::on_uds_response(uint8_t  byte1,
                                      uint8_t  byte2,
                                      float    raw,
                                      uint32_t now_ms)
    {
        for (size_t i = 0; i < SIGNAL_CATALOG_COUNT; i++)
        {
            const SignalSpec &spec = SIGNAL_CATALOG[i];

            /* A signal that owns none of this response's requests can't
             * have changed state, so there's nothing to re-evaluate for
             * it -- skipping straight past keeps the per-frame cost
             * proportional to how many signals actually match rather
             * than re-walking every catalog entry's requests twice. */
            bool matched = false;
            for (uint8_t r = 0; r < spec.num_requests; r++)
            {
                const UdsRequestSpec &req = spec.requests[r];
                if (req.byte1 != byte1 || req.byte2 != byte2)
                {
                    continue;
                }

                request_raw_[i][r]  = (raw + req.offset) * req.scale;
                request_recv_[i][r] = true;
                matched             = true;
            }

            if (!matched)
            {
                continue;
            }

            bool all_received = true;
            for (uint8_t r = 0; r < spec.num_requests; r++)
            {
                if (!request_recv_[i][r])
                {
                    all_received = false;
                    break;
                }
            }

            if (all_received)
            {
                /* set_at(i), not set(spec.id) -- the catalog index is
                 * already in hand here, so re-deriving it from the id
                 * would be a pointless second scan of the catalog. */
                set_at(static_cast<int>(i),
                       spec.transform(request_raw_[i], spec.num_requests),
                       now_ms);
            }
        }
    }

} // namespace core
