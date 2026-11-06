#include "signal_poller.hpp"

namespace core
{

    namespace
    {
        constexpr uint8_t SID_READ_DATA_BY_ID = 0x22;

        /* requestCorrectlyReceived-ResponsePending: the one NRC that does
         * not end a request. Everything else is a final (if unhappy)
         * answer. */
        constexpr uint8_t NRC_RESPONSE_PENDING = 0x78;
    } // namespace

    SignalPoller::SignalPoller(UdsClient &uds, IClock &clock)
        : uds_(uds)
        , clock_(clock)
    {
    }

    int SignalPoller::find_request(uint8_t ecu,
                                   uint8_t byte1,
                                   uint8_t byte2) const
    {
        for (uint8_t i = 0; i < count_; i++)
        {
            if (entries_[i].ecu == ecu && entries_[i].byte1 == byte1
                && entries_[i].byte2 == byte2)
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

    void SignalPoller::set_active(const uint16_t *signal_ids,
                                  uint8_t         count,
                                  uint8_t         priority_count)
    {
        if (signal_ids == nullptr)
        {
            count = 0;
        }
        if (count > MAX_ACTIVE_SIGNALS)
        {
            count = MAX_ACTIVE_SIGNALS;
        }
        if (priority_count > count)
        {
            priority_count = count;
        }

        /* An unchanged list rebuilds to exactly the same table, and
         * rebuilding resets every entry's cadence and backoff state. This
         * is called from a periodic backstop task as well as from the
         * config-change callbacks, so bailing here is what keeps the
         * backstop free. */
        if (count == active_count_ && priority_count == active_priority_count_)
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
        active_count_          = count;
        active_priority_count_ = priority_count;

        uint32_t now = clock_.millis();
        count_       = 0;

        for (uint8_t s = 0; s < active_count_; s++)
        {
            int idx = signal_table_find(active_ids_[s]);
            if (idx < 0)
            {
                continue;
            }

            const SignalSpec &spec     = SIGNAL_CATALOG[idx];
            bool              priority = s < active_priority_count_;
            for (uint8_t r = 0; r < spec.num_requests; r++)
            {
                const UdsRequestSpec &req = spec.requests[r];

                /**
                 * Two displayed signals can share a DID -- Int Pres and
                 * Int Pres Act. both read 0x3E70, Amb Pres and Int Pres
                 * Act. both read 0x13CA -- and that is one request on the
                 * wire, at the faster of the two cadences.
                 * SignalStore::on_uds_response() already fans a single
                 * response out to every catalog entry owning the DID, so
                 * asking twice only ever bought a duplicate frame. The
                 * same DID number on two modules is two requests, though:
                 * it means different things in each.
                 */
                int existing = find_request(req.ecu, req.byte1, req.byte2);
                if (existing >= 0)
                {
                    if (spec.cycle_time_ms < entries_[existing].period_ms)
                    {
                        entries_[existing].period_ms = spec.cycle_time_ms;
                    }
                    /**
                     * One request on the wire, so it's priority if
                     * either signal is: a panel never waits behind the
                     * log just because the profile also wants its DID.
                     */
                    entries_[existing].priority
                        = entries_[existing].priority || priority;
                    continue;
                }

                if (count_ >= MAX_ACTIVE_REQUESTS)
                {
                    break;
                }

                Entry &entry    = entries_[count_++];
                entry.ecu       = req.ecu;
                entry.byte1     = req.byte1;
                entry.byte2     = req.byte2;
                entry.period_ms = spec.cycle_time_ms;
                entry.misses    = 0;
                entry.priority  = priority;
                /* Due immediately: a freshly displayed panel should fill
                 * in as fast as the ECU will answer rather than sitting
                 * on "--" for a cycle. Flow control still serializes the
                 * resulting rush. */
                entry.next_due_ms = now;
            }
        }

        has_background_ = false;
        for (uint8_t i = 0; i < count_; i++)
        {
            has_background_ = has_background_ || !entries_[i].priority;
        }

        /* Whatever was outstanding belongs to the old table, and
         * in_flight_idx_ indexes into one that no longer exists. Its
         * response will still reach SignalStore through
         * UdsClient::poll(); it just no longer gates the next send. */
        in_flight_ = false;
    }

    int SignalPoller::pick_due(uint32_t now_ms) const
    {
        /**
         * Best due entry in each tier: [0] background, [1] priority.
         */
        int     best[2]         = { -1, -1 };
        int32_t best_overdue[2] = { 0, 0 };

        for (uint8_t i = 0; i < count_; i++)
        {
            /* Signed difference so this wraps correctly across millis()
             * overflow, the same way core::Scheduler::tick() relies on
             * unsigned subtraction wrapping. */
            int32_t overdue
                = static_cast<int32_t>(now_ms - entries_[i].next_due_ms);
            if (overdue < 0)
            {
                continue;
            }

            /* Most-overdue-first, not round-robin by index. When demand
             * exceeds what the ECU can serve, index order would let the
             * front of the table permanently starve the back -- which is
             * the failure being fixed here. Overdue-ness accumulates on
             * whoever is being skipped, so every entry in a tier gets
             * served and they degrade proportionally to their cycle
             * times. */
            uint8_t tier = entries_[i].priority ? 1 : 0;
            if (best[tier] < 0 || overdue > best_overdue[tier])
            {
                best[tier]         = static_cast<int>(i);
                best_overdue[tier] = overdue;
            }
        }

        /**
         * Priority first. A background entry goes when nothing priority
         * is due, or once it has waited past BACKGROUND_STARVATION_MS --
         * from then on it simply competes on overdue-ness, which it will
         * usually win, since priority entries are being served promptly
         * and so are never far overdue.
         */
        if (best[1] < 0)
        {
            return best[0];
        }
        if (best[0] >= 0
            && best_overdue[0] >= static_cast<int32_t>(BACKGROUND_STARVATION_MS)
            && best_overdue[0] > best_overdue[1])
        {
            return best[0];
        }
        return best[1];
    }

    void SignalPoller::tick()
    {
        uint32_t now = clock_.millis();

        if (in_flight_)
        {
            int32_t waited = static_cast<int32_t>(now - sent_at_ms_);
            if (waited < static_cast<int32_t>(RESPONSE_TIMEOUT_MS))
            {
                return; /* still waiting -- this is the flow control */
            }
            if (uds_.receiving()
                && waited < static_cast<int32_t>(RESPONSE_TIMEOUT_MS
                                                 + MULTI_FRAME_GRACE_MS))
            {
                return; /* a long answer is part-way in */
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
        request.ecu   = entry.ecu;
        request.byte1 = entry.byte1;
        request.byte2 = entry.byte2;
        uds_.send(request);

        /**
         * `now + period`, not `next_due += period`. Under saturation the
         * latter lets due times fall unboundedly into the past and turns
         * the cadence into a backlog to be worked off; this one means
         * "no faster than period, and as fast as the ECU allows when it
         * cannot keep up", with pick_due() keeping that fair.
         *
         * The exception is a priority request sharing the bus with
         * background ones. It wins every slot it's due for, but can still
         * fall due while a background request is in flight and wait out
         * that round trip -- and from `now`, every cycle would stretch by
         * that wait, letting the log slow the panels after all. So it
         * keeps its schedule (`next_due += period`) while it went out
         * less than a whole period late, and only re-anchors to `now`
         * past that. Equal-period priority entries would share a phase
         * that way and lose some fairness under saturation, which is why
         * this is limited to when there is background traffic to absorb
         * at all.
         */
        uint32_t period   = effective_period(entry);
        uint32_t anchored = entry.next_due_ms + period;
        bool     keep_phase
            = entry.priority && has_background_
              && static_cast<int32_t>(now - anchored) < 0;
        entry.next_due_ms = keep_phase ? anchored : now + period;

        in_flight_     = true;
        in_flight_idx_ = static_cast<uint8_t>(idx);
        sent_at_ms_    = now;
    }

    void SignalPoller::on_response(uint8_t ecu, uint8_t byte1, uint8_t byte2)
    {
        if (!in_flight_)
        {
            return;
        }

        Entry &entry = entries_[in_flight_idx_];
        if (entry.ecu != ecu || entry.byte1 != byte1 || entry.byte2 != byte2)
        {
            /* Some other tester's traffic, or a late answer to a request
             * that already timed out. Either way it is not what this slot
             * is waiting on, so it must not free it. */
            return;
        }

        entry.misses = 0;
        in_flight_   = false;
    }

    void SignalPoller::on_negative_response(uint8_t ecu,
                                            uint8_t service,
                                            uint8_t nrc)
    {
        if (!in_flight_ || service != SID_READ_DATA_BY_ID
            || entries_[in_flight_idx_].ecu != ecu)
        {
            return;
        }

        if (nrc == NRC_RESPONSE_PENDING)
        {
            /* "Still working, keep waiting" -- restarting the deadline is
             * the entire point of this NRC. Freeing the slot instead
             * would put the next request on the wire while the ECU is
             * still composing this one's answer. */
            sent_at_ms_ = clock_.millis();
            return;
        }

        /* Any other NRC is a final answer: the request is done, badly.
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
