/**
 * @file signal_poller.hpp
 * @brief Demand-driven, flow-controlled UDS request scheduler: asks only
 * for the signals something is actually displaying, one request at a time,
 * never sending the next until the last one has been answered, rejected,
 * or timed out.
 *
 * This replaces a one-core::Scheduler-task-per-catalog-entry model that
 * polled all 22 signals regardless of what was on screen and fired every
 * request the instant its cycle timer came due, with nothing tracking
 * whether the ECU had answered the previous one. Since every task was
 * registered in the same boot-time loop they all shared a phase, so those
 * requests left in bursts -- six frames inside ~1.3ms every 50ms, thirteen
 * on alternating ticks, ~203 requests/second in aggregate at a single ECU
 * on 0x7E0. A UDS server handles one request at a time, so the tail of
 * each burst was simply dropped, and whichever signal sat at the tail
 * starved consistently rather than randomly.
 */
#pragma once

#include <stdint.h>

#include "platform.hpp"
#include "signal_source.hpp"
#include "uds_binding.hpp"
#include "uds_client.hpp"

namespace core
{

    class SignalPoller
    {
        public:
            /**
             * Taken from the interface rather than declared again, so a
             * poller can never hold fewer signals than the ISignalSource
             * contract promises callers. See signal_source.hpp for why the
             * number is what it is, and why it is not derived from
             * core::MAX_PANELS directly. */
            static constexpr uint8_t MAX_ACTIVE_SIGNALS
                = ISignalSource::MAX_ACTIVE_SIGNALS;
            static constexpr uint8_t MAX_ACTIVE_REQUESTS
                = MAX_ACTIVE_SIGNALS * MAX_REQUESTS_PER_SIGNAL;

            /**
             * Comfortably past a single-frame 0x22 round trip (single
             * digits to low tens of ms on this ECU) without being so long
             * that one dead DID stalls everything else for a visible
             * beat. An ECU that needs longer is required to say so with
             * an 0x78 NRC, which restarts this rather than expiring it. */
            static constexpr uint32_t RESPONSE_TIMEOUT_MS = 60;

            /**
             * A DID this ECU will never answer -- unsupported on the car,
             * or gated behind a diagnostic session this client does not
             * open -- would otherwise burn a RESPONSE_TIMEOUT_MS slot
             * every period forever, crowding out the signals that do
             * answer. That is the same starvation this class exists to
             * remove, so after a few consecutive misses the entry drops to
             * a slow retry and stays there until one comes back. */
            static constexpr uint8_t  MISSES_BEFORE_BACKOFF = 3;
            static constexpr uint32_t BACKOFF_PERIOD_MS     = 2000;

            SignalPoller(UdsClient &uds, IClock &clock);

            /**
             * Points the poller at the signals currently worth asking for
             * — the displayed panel set. Ids not in the catalog are
             * skipped; anything past MAX_ACTIVE_SIGNALS is ignored.
             *
             * Rebuilding the request table costs a full cadence reset, so
             * an unchanged list is a no-op and this is safe to call on a
             * timer as a backstop against a config path that forgets to.
             */
            void set_active(const uint16_t *signal_ids, uint8_t count);

            /**
             * Sends at most one request, and only when nothing is
             * outstanding and something is due. Call once per main-loop
             * iteration — it paces itself off the clock rather than
             * needing a core::Scheduler task, so a request goes out the
             * moment its predecessor's response has been drained instead
             * of waiting for the next tick boundary.
             */
            void tick();

            /** Feed every decoded UDS response here. */
            void on_response(uint8_t byte1, uint8_t byte2);

            /** Feed every UDS negative response here. */
            void on_negative_response(uint8_t service, uint8_t nrc);

        private:
            struct Entry
            {
                    uint8_t  byte1       = 0;
                    uint8_t  byte2       = 0;
                    uint32_t period_ms   = 0;
                    uint32_t next_due_ms = 0;
                    uint8_t  misses      = 0;
            };

            int      find_request(uint8_t byte1, uint8_t byte2) const;
            int      pick_due(uint32_t now_ms) const;
            uint32_t effective_period(const Entry &entry) const;

            UdsClient &uds_;
            IClock    &clock_;

            Entry   entries_[MAX_ACTIVE_REQUESTS];
            uint8_t count_ = 0;

            uint16_t active_ids_[MAX_ACTIVE_SIGNALS] = {};
            uint8_t  active_count_                   = 0;

            bool     in_flight_     = false;
            uint8_t  in_flight_idx_ = 0;
            uint32_t sent_at_ms_    = 0;
    };

} // namespace core
