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

    return static_cast<float>(static_cast<int32_t>(value * scale + 0.5f))
         / static_cast<float>(scale);
}
} // namespace

SignalValue SignalStore::s_invalid{};

void SignalStore::set(uint16_t id, float value, uint32_t now_ms)
{
    int idx = signal_catalog_find(id);
    if (idx < 0)
    {
        return;
    }

    SignalValue &v   = values_[idx];
    v.value          = value;
    v.has_data       = true;
    v.last_update_ms = now_ms;
}

const SignalValue &SignalStore::get(uint16_t id) const
{
    int idx = signal_catalog_find(id);
    if (idx < 0)
    {
        return s_invalid;
    }
    return values_[idx];
}

float SignalStore::get_display_value(uint16_t id, bool is_metric) const
{
    int idx = signal_catalog_find(id);
    if (idx < 0)
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

        for (uint8_t r = 0; r < spec.num_requests; r++)
        {
            const UdsRequestSpec &req = spec.requests[r];
            if (req.byte1 != byte1 || req.byte2 != byte2)
            {
                continue;
            }

            request_raw_[i][r]  = (raw + req.offset) * req.scale;
            request_recv_[i][r] = true;
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
            set(spec.id,
                spec.transform(request_raw_[i], spec.num_requests),
                now_ms);
        }
    }
}

} // namespace core
