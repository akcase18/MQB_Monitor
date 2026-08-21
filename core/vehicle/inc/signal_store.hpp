/**
 * @file signal_store.hpp
 * @brief Runtime value store for the signals in signal_catalog.hpp. Feeds
 * off UdsClient response decodes: caches each signal's per-request raw
 * values until all of a signal's requests have reported, then runs the
 * signal's transform and records the result.
 */
#pragma once

#include <stdint.h>

#include "signal_catalog.hpp"

namespace core
{

    struct SignalValue
    {
            float    value          = 0.0f;
            bool     has_data       = false;
            uint32_t last_update_ms = 0;
    };

    class SignalStore
    {
        public:
            /**
             * Directly sets a signal's current value, e.g. from
             * on_uds_response() once all its requests have reported.
             */
            void set(uint16_t id, float value, uint32_t now_ms);

            const SignalValue &get(uint16_t id) const;

            /**
             * @returns `id`'s current value converted to the requested unit
             * system and rounded/truncated to that unit type's display
             * precision (ported from the old Info::convert_units/round_data).
             */
            float get_display_value(uint16_t id, bool is_metric) const;

            /* Catalog-index overloads of the three accessors above, for
             * callers that already hold an index (gui::VehicleBinding,
             * core::Logger, on_uds_response() below). The id-keyed forms
             * are just signal_catalog_find() -- a linear scan of the whole
             * catalog -- in front of these, and the hot paths were paying
             * for it two or three times per panel per refresh to look up
             * an index they had already computed. Out-of-range indices
             * behave like an unknown id: no-op / s_invalid / 0.0f. */
            void set_at(int idx, float value, uint32_t now_ms);

            const SignalValue &get_at(int idx) const;

            float get_display_value_at(int idx, bool is_metric) const;

            /**
             * Feeds one decoded UDS response into the store: finds every
             * catalog request matching (byte1, byte2), decodes `raw` with that
             * request's offset/scale, and once all of a signal's requests have
             * reported, recomputes it via SignalSpec::transform.
             */
            void on_uds_response(uint8_t  byte1,
                                 uint8_t  byte2,
                                 float    raw,
                                 uint32_t now_ms);

        private:
            /* const, so it lands in flash rather than costing a
             * SignalValue's worth of RAM for a value that never changes. */
            static const SignalValue s_invalid;

            SignalValue values_[SIGNAL_CATALOG_COUNT];
            float request_raw_[SIGNAL_CATALOG_COUNT][MAX_REQUESTS_PER_SIGNAL]
                = {};
            bool request_recv_[SIGNAL_CATALOG_COUNT][MAX_REQUESTS_PER_SIGNAL]
                = {};
    };

} // namespace core
