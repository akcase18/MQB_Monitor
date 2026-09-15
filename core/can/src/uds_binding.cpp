/**
 * @file uds_binding.cpp
 * @brief Home of the transform_* functions for the UDS binding table. The
 * UDS_BINDINGS[] array itself is GENERATED from core/vehicle/data/mqb/signals.json
 * -- the canonical registry shared with the Mac monitor tool (tools/monitor)
 * -- into uds_binding.generated.inc by tools/gen_catalog.py, alongside
 * core/vehicle's signal_catalog.generated.inc from the same filtered pass.
 * Edit signals.json and re-run that script to change a signal; add a new
 * transform here (and reference it by name in the JSON's "transform" field)
 * to add non-default combining behavior.
 *
 * These transforms used to live in signal_catalog.cpp, next to the catalog
 * they combined values for. They moved here with the rest of the acquisition
 * data so core/vehicle could stop depending on core/can -- see
 * uds_binding.hpp.
 */
#include "uds_binding.hpp"

namespace core
{

    namespace
    {
        float transform_default(const float *raw_values, uint8_t n)
        {
            (void)n;
            return raw_values[0];
        }

        /**
         * Generic two-request combiners for compound signals built via the
         * monitor tool's Registry "New Compound Signal..." dialog -- no new
         * C++ needed per combination. */
        float transform_sum(const float *raw_values, uint8_t n)
        {
            (void)n;
            return raw_values[0] + raw_values[1];
        }

        float transform_difference(const float *raw_values, uint8_t n)
        {
            (void)n;
            return raw_values[1] - raw_values[0];
        }

        float transform_average(const float *raw_values, uint8_t n)
        {
            (void)n;
            return (raw_values[0] + raw_values[1]) * 0.5f;
        }

        float transform_min(const float *raw_values, uint8_t n)
        {
            (void)n;
            return raw_values[0] < raw_values[1] ? raw_values[0]
                                                 : raw_values[1];
        }

        float transform_max(const float *raw_values, uint8_t n)
        {
            (void)n;
            return raw_values[0] > raw_values[1] ? raw_values[0]
                                                 : raw_values[1];
        }
    } // namespace

#include "uds_binding.generated.inc"

    static_assert(sizeof(UDS_BINDINGS) / sizeof(UDS_BINDINGS[0])
                      == UDS_BINDING_COUNT,
                  "UDS_BINDING_COUNT must match UDS_BINDINGS's entry count");

    int uds_binding_find(uint16_t signal_id)
    {
        for (size_t i = 0; i < UDS_BINDING_COUNT; i++)
        {
            if (UDS_BINDINGS[i].signal_id == signal_id)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

} // namespace core
