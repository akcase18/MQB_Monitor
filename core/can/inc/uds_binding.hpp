/**
 * @file uds_binding.hpp
 * @brief How each catalog signal is acquired over UDS: which requests feed
 * it, how often to ask, and how to combine the decoded values.
 *
 * This is the half of the old SignalSpec that knew about CAN. Splitting it
 * out is what lets core/vehicle's catalog -- and therefore core/config,
 * gui/bindings and everything else that includes signal_catalog.hpp -- stop
 * depending on core/can entirely. A second product acquiring the same
 * signals from analog sensors supplies its own binding table instead of this
 * one, and nothing in core/vehicle changes.
 *
 * INVARIANT: UDS_BINDINGS is a PARALLEL ARRAY to core::SIGNAL_CATALOG --
 * same length, same order, so UDS_BINDINGS[i] describes SIGNAL_CATALOG[i].
 * Both are generated from the same filtered pass over signals.json by
 * tools/gen_catalog.py, so the correspondence is guaranteed by
 * construction rather than maintained by hand. `signal_id` is carried in
 * each entry anyway so a drift is detectable (test_core_vehicle asserts
 * it), and uds_binding_find() exists for callers holding an id rather than
 * an index.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "uds_client.hpp"

namespace core
{

    /**
     * Combines a signal's decoded, already offset/scaled per-request values
     * into its final value. Most signals are a straight passthrough of
     * request 0; compound signals combine two.
     */
    using SignalTransform = float (*)(const float *raw_values, uint8_t n);

    struct UdsSignalBinding
    {
            /** Matches core::SignalSpec::id at the same array index. */
            uint16_t signal_id;

            /** Polling floor, not a fixed period -- see SignalPoller. */
            uint16_t cycle_time_ms;

            UdsRequestSpec requests[MAX_REQUESTS_PER_SIGNAL];
            uint8_t        num_requests;

            SignalTransform transform;
    };

    extern const UdsSignalBinding UDS_BINDINGS[];

    /**
     * UDS_BINDING_COUNT -- generated per product alongside the
     * catalog it parallels. See signal_catalog.hpp. */
#include "uds_binding_count.inc"

    /**
     * @returns The binding index for `signal_id`, or -1 if not found. Equal
     * to the catalog index for the same id (see the file's INVARIANT).
     */
    int uds_binding_find(uint16_t signal_id);

} // namespace core
