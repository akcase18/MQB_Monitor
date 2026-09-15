/**
 * @file signal_store.hpp
 * @brief Runtime value store for the signals in signal_catalog.hpp.
 *
 * Deliberately knows nothing about where a value came from: set_at() is the
 * only write path, and it means "somebody measured this signal, at this
 * time". core/can's UdsDecoder fills it from CAN responses; an analog
 * sensor source on another product fills the same store the same way.
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
             * Directly sets a signal's current value -- e.g. from
             * core::UdsDecoder once all of a signal's requests have
             * reported, or from an analog sensor source once its channel
             * has been conditioned.
             */
            void set(uint16_t id, float value, uint32_t now_ms);

            const SignalValue &get(uint16_t id) const;

            /**
             * @returns `id`'s current value converted to the requested unit
             * system and rounded/truncated to that unit type's display
             * precision (ported from the old Info::convert_units/round_data).
             */
            float get_display_value(uint16_t id, bool is_metric) const;

            /**
             * Catalog-index overloads of the three accessors above, for
             * callers that already hold an index (gui::VehicleBinding,
             * core::Logger, core::UdsDecoder). The id-keyed forms are just
             * signal_catalog_find() -- a linear scan of the whole catalog
             * -- in front of these, and the hot paths were paying for it
             * two or three times per panel per refresh to look up an index
             * they had already computed. Out-of-range indices behave like
             * an unknown id: no-op / s_invalid / 0.0f. */
            void set_at(int idx, float value, uint32_t now_ms);

            const SignalValue &get_at(int idx) const;

            float get_display_value_at(int idx, bool is_metric) const;

        private:
            /**
             * const, so it lands in flash rather than costing a
             * SignalValue's worth of RAM for a value that never changes. */
            static const SignalValue s_invalid;

            SignalValue values_[SIGNAL_CATALOG_COUNT];
    };

} // namespace core
