#include "logger.hpp"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace core
{

    void chunk_path(uint16_t chunk_index, char *out)
    {
        snprintf(out, CHUNK_PATH_LEN, "/log_%03u.bin", chunk_index);
    }

    Logger::Logger(IStorage &backing,
                   uint32_t  chunk_size_bytes,
                   uint16_t  chunk_count)
        : backing_(backing)
        , chunk_size_bytes_(chunk_size_bytes)
        , chunk_count_(chunk_count)
    {
        /* NaN compares unequal to every value including itself, which is
         * exactly the "nothing logged for this signal yet, so whatever
         * arrives first is new" baseline sample() wants -- see the member
         * declaration. Not expressible as a member initializer on an
         * array, hence the loop. */
        for (uint16_t i = 0; i < SIGNAL_CATALOG_COUNT; i++)
        {
            last_value_[i] = NAN;
        }
    }

    bool Logger::begin()
    {
        if (!backing_.is_present())
        {
            ready_ = false;
            return false;
        }

        /* Recover rotation state purely from what's already on disk --
         * see the header comment on begin(). */
        bool     has_any        = false;
        uint32_t max_chunk_seq  = 0;
        uint32_t max_session_id = 0;

        for (uint16_t i = 0; i < chunk_count_; i++)
        {
            char path[CHUNK_PATH_LEN];
            chunk_path(i, path);

            if (!backing_.open(path, StorageFileMode::Read))
            {
                continue; /* never written this slot -- fine, skip it */
            }

            ChunkHeader header {};
            size_t      n = backing_.read(&header, sizeof(header));
            backing_.close();

            if (n != sizeof(header) || header.magic != LOG_MAGIC
                || header.version != LOG_VERSION)
            {
                continue; /* blank/corrupt/foreign -- self-heals by
                           * treating it the same as never-written */
            }

            if (!has_any || header.chunk_seq > max_chunk_seq)
            {
                max_chunk_seq = header.chunk_seq;
            }
            if (header.session_id > max_session_id)
            {
                max_session_id = header.session_id;
            }
            has_any = true;
        }

        session_id_           = max_session_id + 1;
        next_chunk_seq_        = has_any ? max_chunk_seq + 1 : 0;
        current_chunk_index_
            = static_cast<uint16_t>(next_chunk_seq_ % chunk_count_);

        start_new_chunk();

        ready_ = true;
        return true;
    }

    void Logger::sample(const SignalStore &signals,
                        const Config      &config,
                        uint32_t           now_ms)
    {
        if (!ready_)
        {
            return;
        }

        uint8_t panels = config.settings.num_panels;
        if (panels > MAX_PANELS)
        {
            panels = MAX_PANELS; /* defensive -- a corrupt config
                                  * shouldn't run off the end of
                                  * priorities[] */
        }

        bool now_displayed[SIGNAL_CATALOG_COUNT] = {};

        for (uint8_t p = 0; p < panels; p++)
        {
            int idx = signal_catalog_find(config.priorities[p].signal_id);
            if (idx < 0)
            {
                continue; /* id not in this build's catalog */
            }

            now_displayed[idx] = true;

            /* Index-keyed: get(id) would re-scan the whole catalog to
             * recover the index signal_catalog_find() just produced, and
             * this runs for every panel on every sample tick. */
            const SignalValue &v = signals.get_at(idx);

            if (!in_display_set_[idx])
            {
                /* Just came on screen -- take whatever it already holds
                 * as the baseline to compare against, without writing it.
                 * That value may be arbitrarily stale (every catalog
                 * signal is polled whether or not it's displayed), and
                 * stale data has no business in the log. */
                if (v.has_data)
                {
                    last_seen_ms_[idx] = v.last_update_ms;
                    last_value_[idx]   = v.value;
                }
                continue;
            }

            if (!v.has_data)
            {
                continue;
            }

            /* Rate floor. Also subsumes "hasn't been polled since we last
             * looked" (elapsed == 0), so no separate freshness check is
             * needed. Unsigned subtraction, so millis() wraparound is
             * handled without a special case. */
            if ((v.last_update_ms - last_seen_ms_[idx]) < LOG_MIN_INTERVAL_MS)
            {
                continue;
            }

            /* The actual on-change test -- last_update_ms advances on
             * every poll response regardless of whether the value moved,
             * so only the value itself can tell a real change from a
             * repeat. */
            if (v.value == last_value_[idx])
            {
                continue;
            }

            /* Keyed by catalog position rather than panel slot, so two
             * panels showing the same signal collapse to one record per
             * change rather than logging it twice. */
            last_seen_ms_[idx] = v.last_update_ms;
            last_value_[idx]   = v.value;

            /* Silently drops the record if the buffer is somehow already
             * full (at most MAX_PANELS new records land per tick, far
             * below BUFFER_CAPACITY -- this is just a defensive bound,
             * not an expected path). */
            if (buffer_count_ < BUFFER_CAPACITY)
            {
                buffer_[buffer_count_++]
                    = { v.last_update_ms, SIGNAL_CATALOG[idx].id, v.value };
            }
        }

        memcpy(in_display_set_, now_displayed, sizeof(in_display_set_));

        bool due = (now_ms - last_flush_ms_) >= FLUSH_INTERVAL_MS;
        if (buffer_count_ >= BUFFER_CAPACITY || due)
        {
            flush();
            last_flush_ms_ = now_ms;
        }
    }

    void Logger::flush()
    {
        if (buffer_count_ == 0)
        {
            return;
        }

        char path[CHUNK_PATH_LEN];
        chunk_path(current_chunk_index_, path);

        if (backing_.open(path, StorageFileMode::Append))
        {
            size_t bytes
                = static_cast<size_t>(buffer_count_) * sizeof(LogRecord);
            backing_.write(buffer_, bytes);
            backing_.close();
            chunk_bytes_written_ += static_cast<uint32_t>(bytes);
        }

        buffer_count_ = 0;

        if (chunk_bytes_written_ >= chunk_size_bytes_)
        {
            next_chunk_seq_++;
            current_chunk_index_
                = static_cast<uint16_t>(next_chunk_seq_ % chunk_count_);
            start_new_chunk();
        }
    }

    void Logger::start_new_chunk()
    {
        char path[CHUNK_PATH_LEN];
        chunk_path(current_chunk_index_, path);

        if (backing_.open(path, StorageFileMode::Write))
        {
            ChunkHeader header {
                LOG_MAGIC, LOG_VERSION, session_id_, next_chunk_seq_
            };
            backing_.write(&header, sizeof(header));
            backing_.close();
        }

        chunk_bytes_written_ = sizeof(ChunkHeader);
    }

} // namespace core
