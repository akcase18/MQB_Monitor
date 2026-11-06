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
#include "signal_catalog.hpp"
#include "uds_client.hpp"

namespace core
{

    class SignalPoller
    {
        public:
            /**
             * Sized for a full screen of panels (core::MAX_PANELS), a
             * full log profile (core::MAX_LOG_SIGNALS) none of which is on
             * screen, and every internal signal the dash reads for itself
             * (Dim Ctrl, for auto brightness), each owning the catalog
             * maximum of DIDs, assuming no dedup. The counts are spelled
             * out here rather than derived from core/config so core/vehicle
             * keeps its independence from it -- main.cpp, which already
             * depends on both, is what translates the displayed panel set
             * and the profile into the id list set_active() takes, and
             * static_asserts that it fits.
             */
            static constexpr uint8_t MAX_ACTIVE_SIGNALS
                = 6 + 16 + INTERNAL_SIGNAL_COUNT;
            static constexpr uint8_t MAX_ACTIVE_REQUESTS
                = MAX_ACTIVE_SIGNALS * MAX_REQUESTS_PER_SIGNAL;

            /**
             * Comfortably past a single-frame 0x22 round trip (single
             * digits to low tens of ms on this ECU) without being so long
             * that one dead DID stalls everything else for a visible
             * beat. An ECU that needs longer is required to say so with
             * an 0x78 NRC, which restarts this rather than expiring it.
             */
            static constexpr uint32_t RESPONSE_TIMEOUT_MS = 60;

            /**
             * How long past RESPONSE_TIMEOUT_MS a multi-frame answer that
             * has started arriving (UdsClient::receiving()) is waited for.
             * Each block of consecutive frames needs a main-loop pass to
             * drain and answer with flow control, so a long answer can
             * outlast the single-frame deadline without anything being
             * wrong; this caps it if the module stops part-way.
             */
            static constexpr uint32_t MULTI_FRAME_GRACE_MS = 200;

            /**
             * A DID this ECU will never answer -- unsupported on the car,
             * or gated behind a diagnostic session this client does not
             * open -- would otherwise burn a RESPONSE_TIMEOUT_MS slot
             * every period forever, crowding out the signals that do
             * answer. That is the same starvation this class exists to
             * remove, so after a few consecutive misses the entry drops to
             * a slow retry and stays there until one comes back.
             */
            static constexpr uint8_t  MISSES_BEFORE_BACKOFF = 3;
            static constexpr uint32_t BACKOFF_PERIOD_MS     = 2000;

            /**
             * How overdue a background (log-only) request may get before
             * it competes with the priority ones on equal terms. Priority
             * requests win the bus otherwise, so the panels stay
             * responsive while a log profile is being recorded -- but six
             * 50ms engine signals on screen can keep the ECU busy on their
             * own, and without this floor a background request would then
             * never go out at all. Its signal still logs, just no faster
             * than about once per (its period + this).
             */
            static constexpr uint32_t BACKGROUND_STARVATION_MS = 1000;

            SignalPoller(UdsClient &uds, IClock &clock);

            /**
             * Points the poller at the signals currently worth asking for
             * — the displayed panel set and any internal signal in use,
             * then whatever a log profile adds. Ids in neither part of the
             * catalog are skipped; anything past MAX_ACTIVE_SIGNALS is
             * ignored.
             *
             * The first `priority_count` ids are priority: when the ECU
             * can't keep up, their requests go out first, and the rest
             * (background) take what's left -- see
             * BACKGROUND_STARVATION_MS. A request two signals share is
             * priority if either is. The default makes every id priority.
             *
             * Rebuilding the request table costs a full cadence reset, so
             * an unchanged list is a no-op and this is safe to call on a
             * timer as a backstop against a config path that forgets to.
             */
            void set_active(const uint16_t *signal_ids,
                            uint8_t         count,
                            uint8_t         priority_count = 0xFF);

            /**
             * Sends at most one request, and only when nothing is
             * outstanding and something is due. Call once per main-loop
             * iteration — it paces itself off the clock rather than
             * needing a core::Scheduler task, so a request goes out the
             * moment its predecessor's response has been drained instead
             * of waiting for the next tick boundary.
             */
            void tick();

            /**
             * Feed every UDS response here, with the module (ECU table
             * index) that sent it.
             */
            void on_response(uint8_t ecu, uint8_t byte1, uint8_t byte2);

            /**
             * Feed every UDS negative response here.
             */
            void on_negative_response(uint8_t ecu, uint8_t service, uint8_t nrc);

        private:
            struct Entry
            {
                    uint8_t  ecu         = 0;
                    uint8_t  byte1       = 0;
                    uint8_t  byte2       = 0;
                    uint32_t period_ms   = 0;
                    uint32_t next_due_ms = 0;
                    uint8_t  misses      = 0;
                    bool     priority    = true;
            };

            int find_request(uint8_t ecu, uint8_t byte1, uint8_t byte2) const;
            int      pick_due(uint32_t now_ms) const;
            uint32_t effective_period(const Entry &entry) const;

            UdsClient &uds_;
            IClock    &clock_;

            Entry   entries_[MAX_ACTIVE_REQUESTS];
            uint8_t count_ = 0;

            uint16_t active_ids_[MAX_ACTIVE_SIGNALS] = {};
            uint8_t  active_count_                   = 0;
            uint8_t  active_priority_count_          = 0;
            /**
             * Whether any entry is background -- see tick()'s cadence.
             */
            bool     has_background_                 = false;

            bool     in_flight_     = false;
            uint8_t  in_flight_idx_ = 0;
            uint32_t sent_at_ms_    = 0;
    };

} // namespace core
