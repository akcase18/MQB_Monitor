#include "signal_poller.hpp"

namespace core
{

    namespace
    {
        constexpr uint8_t SID_READ_DATA_BY_ID = 0x22;

        /**
         * requestCorrectlyReceived-ResponsePending: the one NRC that does
         * not end a request. Everything else is a final (if unhappy)
         * answer. */
        constexpr uint8_t NRC_RESPONSE_PENDING = 0x78;
    } // namespace

    SignalPoller::SignalPoller(UdsClient &uds, IClock &clock)
        : uds_(uds)
        , clock_(clock)
    {
    }

    int SignalPoller::find_request(uint8_t byte1, uint8_t byte2) const
    {
        for (uint8_t i = 0; i < count_; i++)
        {
            if (entries_[i].byte1 == byte1 && entries_[i].byte2 == byte2)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    uint32_t SignalPoller::effective_period(const Entry &entry) const
    {
        if (entry.misses < MISSES_BEFORE_BACKOFF)
        {
            return entry.period_ms;
        }
        return entry.period_ms > BACKOFF_PERIOD_MS ? entry.period_ms
                                                   : BACKOFF_PERIOD_MS;
    }

    void SignalPoller::set_active(const uint16_t *signal_ids, uint8_t count)
    {
        if (signal_ids == nullptr)
        {
            count = 0;
        }
        if (count > MAX_ACTIVE_SIGNALS)
        {
            count = MAX_ACTIVE_SIGNALS;
        }

        /**
         * An unchanged list rebuilds to exactly the same table, and
         * rebuilding resets every entry's cadence and backoff state. This
         * is called from a periodic backstop task as well as from the
         * config-change callbacks, so bailing here is what keeps the
         * backstop free. */
        if (count == active_count_)
        {
            bool same = true;
            for (uint8_t i = 0; i < count; i++)
            {
                if (active_ids_[i] != signal_ids[i])
                {
                    same = false;
                    break;
                }
            }
            if (same)
            {
                return;
            }
        }

        for (uint8_t i = 0; i < count; i++)
        {
            active_ids_[i] = signal_ids[i];
        }
        active_count_ = count;

        uint32_t now = clock_.millis();
        count_       = 0;

        for (uint8_t s = 0; s < active_count_; s++)
        {
            int idx = uds_binding_find(active_ids_[s]);
            if (idx < 0)
            {
                continue;
            }

            const UdsSignalBinding &binding = UDS_BINDINGS[idx];
            for (uint8_t r = 0; r < binding.num_requests; r++)
            {
                const UdsRequestSpec &req = binding.requests[r];

                /**
                 * Two displayed signals can share a DID -- Int Pres and
                 * Int Pres Act. both read 0x3E70, Amb Pres and Int Pres
                 * Act. both read 0x13CA -- and that is one request on the
                 * wire, at the faster of the two cadences.
                 * SignalStore::on_uds_response() already fans a single
                 * response out to every catalog entry owning the DID, so
                 * asking twice only ever bought a duplicate frame. */
                int existing = find_request(req.byte1, req.byte2);
                if (existing >= 0)
                {
                    if (binding.cycle_time_ms < entries_[existing].period_ms)
                    {
                        entries_[existing].period_ms = binding.cycle_time_ms;
                    }
                    continue;
                }

                if (count_ >= MAX_ACTIVE_REQUESTS)
                {
                    break;
                }

                Entry &entry    = entries_[count_++];
                entry.byte1     = req.byte1;
                entry.byte2     = req.byte2;
                entry.period_ms = binding.cycle_time_ms;
                entry.misses    = 0;
                /**
                 * Due immediately: a freshly displayed panel should fill
                 * in as fast as the ECU will answer rather than sitting
                 * on "--" for a cycle. Flow control still serializes the
                 * resulting rush. */
                entry.next_due_ms = now;
            }
        }

        /**
         * Whatever was outstanding belongs to the old table, and
         * in_flight_idx_ indexes into one that no longer exists. Its
         * response will still reach SignalStore through
         * UdsClient::poll(); it just no longer gates the next send. */
        in_flight_ = false;
    }

    int SignalPoller::pick_due(uint32_t now_ms) const
    {
        int     best         = -1;
        int32_t best_overdue = 0;

        for (uint8_t i = 0; i < count_; i++)
        {
            /**
             * Signed difference so this wraps correctly across millis()
             * overflow, the same way core::Scheduler::tick() relies on
             * unsigned subtraction wrapping. */
            int32_t overdue
                = static_cast<int32_t>(now_ms - entries_[i].next_due_ms);
            if (overdue < 0)
            {
                continue;
            }

            /**
             * Most-overdue-first, not round-robin by index. When demand
             * exceeds what the ECU can serve, index order would let the
             * front of the table permanently starve the back -- which is
             * the failure being fixed here. Overdue-ness accumulates on
             * whoever is being skipped, so every entry gets served and
             * they degrade proportionally to their cycle times. */
            if (best < 0 || overdue > best_overdue)
            {
                best         = static_cast<int>(i);
                best_overdue = overdue;
            }
        }

        return best;
    }

    void SignalPoller::tick()
    {
        uint32_t now = clock_.millis();

        if (in_flight_)
        {
            if (static_cast<int32_t>(now - sent_at_ms_)
                < static_cast<int32_t>(RESPONSE_TIMEOUT_MS))
            {
                return; /* still waiting -- this is the flow control */
            }

            Entry &entry = entries_[in_flight_idx_];
            if (entry.misses < MISSES_BEFORE_BACKOFF)
            {
                entry.misses++;
            }
            in_flight_ = false;
        }

        int idx = pick_due(now);
        if (idx < 0)
        {
            return;
        }

        Entry &entry = entries_[idx];

        UdsRequestSpec request {};
        request.byte1 = entry.byte1;
        request.byte2 = entry.byte2;
        uds_.send(request);

        /**
         * `now + period`, not `next_due += period`. Under saturation the
         * latter lets due times fall unboundedly into the past and turns
         * the cadence into a backlog to be worked off; this one means
         * "no faster than period, and as fast as the ECU allows when it
         * cannot keep up", with pick_due() keeping that fair. */
        entry.next_due_ms = now + effective_period(entry);

        in_flight_     = true;
        in_flight_idx_ = static_cast<uint8_t>(idx);
        sent_at_ms_    = now;
    }

    void SignalPoller::on_response(uint8_t byte1, uint8_t byte2)
    {
        if (!in_flight_)
        {
            return;
        }

        Entry &entry = entries_[in_flight_idx_];
        if (entry.byte1 != byte1 || entry.byte2 != byte2)
        {
            /**
             * Some other tester's traffic, or a late answer to a request
             * that already timed out. Either way it is not what this slot
             * is waiting on, so it must not free it. */
            return;
        }

        entry.misses = 0;
        in_flight_   = false;
    }

    void SignalPoller::on_negative_response(uint8_t service, uint8_t nrc)
    {
        if (!in_flight_ || service != SID_READ_DATA_BY_ID)
        {
            return;
        }

        if (nrc == NRC_RESPONSE_PENDING)
        {
            /**
             * "Still working, keep waiting" -- restarting the deadline is
             * the entire point of this NRC. Freeing the slot instead
             * would put the next request on the wire while the ECU is
             * still composing this one's answer. */
            sent_at_ms_ = clock_.millis();
            return;
        }

        /**
         * Any other NRC is a final answer: the request is done, badly.
         * Releasing the slot now rather than sitting out the full timeout
         * is most of the point of reading these at all. It counts as a
         * miss, so a DID this ECU consistently rejects backs off exactly
         * like one that never answers. */
        Entry &entry = entries_[in_flight_idx_];
        if (entry.misses < MISSES_BEFORE_BACKOFF)
        {
            entry.misses++;
        }
        in_flight_ = false;
    }

} // namespace core
