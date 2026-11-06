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
        /**
         * NaN compares unequal to every value including itself, which is
         * exactly the "nothing logged for this signal yet, so whatever
         * arrives first is new" baseline sample() wants -- see the member
         * declaration. Not expressible as a member initializer on an
         * array, hence the loop.
         */
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

        /**
         * Recover rotation state purely from what's already on disk --
         * see the header comment on begin().
         */
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
                /**
                 * Blank/corrupt/foreign -- self-heals by treating it the same
                 * as never-written.
                 */
                continue;
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

        /**
         * Deliberately does NOT create a chunk here. Creating one costs
         * a ring slot (start_new_chunk() truncates whatever occupied it),
         * and booting is not recording -- now that logging is opt-in, a
         * run of ignition cycles with the recorder off would otherwise
         * chew through the whole ring and destroy captures the user never
         * touched. The first flush() of the first capture creates it.
         */
        chunk_open_ = false;

        /**
         * After rotation state is known, so prune_index() can tell which
         * captures the ring has already lapped past.
         */
        load_index();
        prune_index();
        save_index();

        ready_ = true;
        return true;
    }

    bool Logger::erase()
    {
        /**
         * Dropped before the format, not after: begin() below rebuilds
         * everything that describes what is *on disk*, but knows nothing
         * about the in-RAM tail of a capture in progress. See the header
         * comment for what each of these would otherwise survive into.
         */
        buffer_count_ = 0;
        chunk_open_   = false;
        was_enabled_  = false;
        for (uint16_t i = 0; i < SIGNAL_CATALOG_COUNT; i++)
        {
            in_record_set_[i] = false;
        }

        if (!backing_.format())
        {
            ready_ = false;
            return false;
        }

        /**
         * Rebuilds rotation state and the index against the empty store,
         * which is what leaves log_count() honestly at zero.
         */
        return begin();
    }

    uint32_t Logger::oldest_live_chunk_seq() const
    {
        if (!chunk_open_ && next_chunk_seq_ == 0)
        {
            return 0; /* nothing has ever been written */
        }

        /**
         * next_chunk_seq_ is the open chunk's seq while one is open, and
         * the seq the *next* one will take otherwise -- so the newest
         * chunk actually on disk is one back in the latter case.
         */
        uint32_t newest = chunk_open_ ? next_chunk_seq_ : next_chunk_seq_ - 1;

        /**
         * The ring holds the chunk_count_ most recent seqs ending there.
         * Before the first full lap nothing has been evicted yet.
         */
        if (newest + 1 < chunk_count_)
        {
            return 0;
        }
        return newest - (chunk_count_ - 1);
    }

    void Logger::prune_index()
    {
        uint32_t oldest = oldest_live_chunk_seq();

        /**
         * entries_ is oldest-first, so everything evicted is a prefix --
         * find how much and shift the rest down.
         */
        uint16_t drop = 0;
        while (drop < entry_count_ && entries_[drop].last_chunk_seq < oldest)
        {
            drop++;
        }
        if (drop == 0)
        {
            return;
        }

        for (uint16_t i = drop; i < entry_count_; i++)
        {
            entries_[i - drop] = entries_[i];
        }
        entry_count_ = static_cast<uint16_t>(entry_count_ - drop);
    }

    void Logger::load_index()
    {
        entry_count_ = 0;
        next_log_id_ = 1;

        if (!backing_.open(LOG_INDEX_PATH, StorageFileMode::Read))
        {
            return; /* never written -- start empty */
        }

        LogIndexHeader header {};
        size_t         n = backing_.read(&header, sizeof(header));
        if (n != sizeof(header) || header.magic != LOG_INDEX_MAGIC
            || header.version != LOG_INDEX_VERSION
            || header.entry_count > MAX_LOG_ENTRIES)
        {
            backing_.close();
            /**
             * Corrupt/foreign -- same self-healing treatment a bad chunk
             * header gets: treat as never-written.
             */
            return;
        }

        for (uint16_t i = 0; i < header.entry_count; i++)
        {
            LogIndexEntry entry {};
            if (backing_.read(&entry, sizeof(entry)) != sizeof(entry))
            {
                /**
                 * Truncated (power cut mid-save) -- keep what parsed cleanly
                 * and drop the rest.
                 */
                break;
            }
            entries_[entry_count_++] = entry;
        }
        backing_.close();

        next_log_id_ = header.next_log_id;
        if (next_log_id_ == 0)
        {
            next_log_id_ = 1;
        }
    }

    void Logger::save_index()
    {
        if (!backing_.open(LOG_INDEX_PATH, StorageFileMode::Write))
        {
            return;
        }

        LogIndexHeader header { LOG_INDEX_MAGIC,
                                LOG_INDEX_VERSION,
                                next_log_id_,
                                entry_count_ };
        backing_.write(&header, sizeof(header));
        if (entry_count_ > 0)
        {
            backing_.write(entries_,
                           static_cast<size_t>(entry_count_)
                               * sizeof(LogIndexEntry));
        }
        backing_.close();
    }

    void Logger::begin_capture(uint32_t now_ms)
    {
        if (entry_count_ == MAX_LOG_ENTRIES)
        {
            /**
             * Full -- drop the oldest, which is the next one the ring
             * would have evicted anyway.
             */
            for (uint16_t i = 1; i < entry_count_; i++)
            {
                entries_[i - 1] = entries_[i];
            }
            entry_count_--;
        }

        entries_[entry_count_++] = { next_log_id_++,
                                     session_id_,
                                     next_chunk_seq_,
                                     next_chunk_seq_,
                                     0 };
        active_capture_start_ms_ = now_ms;
        last_index_save_ms_      = now_ms;
        save_index();
    }

    void Logger::end_capture(uint32_t now_ms)
    {
        if (entry_count_ > 0)
        {
            LogIndexEntry &active = entries_[entry_count_ - 1];
            active.last_chunk_seq = next_chunk_seq_;
            active.duration_ms    = now_ms - active_capture_start_ms_;
        }
        save_index();
    }

    uint64_t Logger::used_bytes() const
    {
        uint64_t capacity = ring_capacity_bytes();

        /**
         * next_chunk_seq_ is also the count of chunks completed before
         * the current one, since seqs run 0,1,2,... without gaps. Once
         * that reaches chunk_count_ every slot has been written at least
         * once and the ring is full for good.
         */
        uint64_t completed = next_chunk_seq_;
        if (completed >= chunk_count_)
        {
            return capacity;
        }

        uint64_t used = completed * static_cast<uint64_t>(chunk_size_bytes_);
        if (chunk_open_)
        {
            used += chunk_bytes_written_;
        }

        /**
         * A chunk rotates at >= chunk_size_bytes_, so the last one can
         * overshoot slightly; don't report more than the ring holds.
         */
        return used > capacity ? capacity : used;
    }

    uint32_t Logger::total_logged_ms() const
    {
        uint32_t total = 0;
        for (uint16_t i = 0; i < entry_count_; i++)
        {
            total += entries_[i].duration_ms;
        }
        return total;
    }

    void Logger::sample(const SignalStore &signals,
                        bool               enabled,
                        const uint16_t    *ids,
                        uint8_t            count,
                        uint32_t           now_ms)
    {
        if (!ready_)
        {
            return;
        }

        /**
         * Logging is opt-in (see Settings::logging_enabled). Passed in
         * every tick for the same reason the set is -- the toggle takes
         * effect on the next tick with nothing to keep in sync -- and
         * checked before anything else so the cost while off is one bool
         * test per tick.
         */
        if (!enabled)
        {
            if (was_enabled_)
            {
                /**
                 * Don't strand whatever was buffered when the user
                 * switched off. Clearing in_record_set_ makes every
                 * signal start over from a fresh value when logging
                 * resumes, so the gap doesn't get bridged by a stale
                 * pre-pause one. flush() first, so end_capture() records
                 * the chunk the last records actually landed in.
                 */
                flush();
                for (uint16_t i = 0; i < SIGNAL_CATALOG_COUNT; i++)
                {
                    in_record_set_[i] = false;
                }
                was_enabled_ = false;
                end_capture(now_ms);
            }
            return;
        }

        if (!was_enabled_)
        {
            /**
             * Switching on opens a new capture -- a new "log" in the
             * user's sense, independent of session_id (which only counts
             * power cycles). Several captures per boot is the normal
             * case.
             */
            was_enabled_ = true;
            begin_capture(now_ms);
        }

        if (ids == nullptr)
        {
            count = 0;
        }

        bool now_in_set[SIGNAL_CATALOG_COUNT] = {};

        for (uint8_t i = 0; i < count; i++)
        {
            int idx = signal_catalog_find(ids[i]);
            if (idx < 0 || now_in_set[idx])
            {
                /**
                 * Not in this build's catalog, or a repeat -- two panels
                 * showing one signal log it once.
                 */
                continue;
            }

            now_in_set[idx] = true;

            /**
             * Index-keyed: get(id) would re-scan the whole catalog to
             * recover the index signal_catalog_find() just produced, and
             * this runs for every signal on every sample tick.
             */
            const SignalValue &v = signals.get_at(idx);

            if (!in_record_set_[idx])
            {
                /**
                 * Just entered. Whatever the store holds now was there
                 * before anything asked for it -- see sample()'s doc
                 * comment -- so only note when, and wait for a fresher
                 * value.
                 */
                entered_ms_[idx]     = now_ms;
                awaiting_first_[idx] = true;
                continue;
            }

            if (!v.has_data)
            {
                continue;
            }

            if (awaiting_first_[idx])
            {
                /**
                 * Signed, so millis() wraparound is handled. Equal counts
                 * as stale: an update stamped the same millisecond as
                 * entry may well have landed before it.
                 */
                if (static_cast<int32_t>(v.last_update_ms - entered_ms_[idx])
                    <= 0)
                {
                    continue;
                }
                awaiting_first_[idx] = false;
            }
            else
            {
                /**
                 * Rate floor. Also subsumes "hasn't been polled since we
                 * last looked" (elapsed == 0), so no separate freshness
                 * check is needed. Unsigned subtraction, so millis()
                 * wraparound is handled without a special case.
                 */
                if ((v.last_update_ms - last_seen_ms_[idx])
                    < LOG_MIN_INTERVAL_MS)
                {
                    continue;
                }

                /**
                 * The actual on-change test -- last_update_ms advances on
                 * every poll response regardless of whether the value
                 * moved, so only the value itself can tell a real change
                 * from a repeat.
                 */
                if (v.value == last_value_[idx])
                {
                    continue;
                }
            }

            last_seen_ms_[idx] = v.last_update_ms;
            last_value_[idx]   = v.value;

            /**
             * Defensive bound only: the flush below always leaves room
             * for a whole tick's worth of records.
             */
            if (buffer_count_ < BUFFER_CAPACITY)
            {
                buffer_[buffer_count_++]
                    = { v.last_update_ms, SIGNAL_CATALOG[idx].id, v.value };
            }
        }

        memcpy(in_record_set_, now_in_set, sizeof(in_record_set_));

        /**
         * Flush while there's still room for another tick of this size,
         * rather than once the buffer is full: a 16-signal profile can
         * add 16 records in one tick, and a full buffer would drop them.
         */
        bool due  = (now_ms - last_flush_ms_) >= FLUSH_INTERVAL_MS;
        bool full = buffer_count_ + count > BUFFER_CAPACITY;
        if (full || due)
        {
            flush();
            last_flush_ms_ = now_ms;
        }

        /**
         * Extend the open capture to wherever writing has reached.
         * Recomputed from its start rather than accumulated, so it can't
         * drift.
         */
        if (entry_count_ > 0)
        {
            LogIndexEntry &active  = entries_[entry_count_ - 1];
            active.last_chunk_seq  = next_chunk_seq_;
            active.duration_ms     = now_ms - active_capture_start_ms_;
        }

        if ((now_ms - last_index_save_ms_) >= INDEX_SAVE_INTERVAL_MS)
        {
            save_index();
            last_index_save_ms_ = now_ms;
        }
    }

    void Logger::flush()
    {
        if (buffer_count_ == 0)
        {
            return;
        }

        if (!chunk_open_)
        {
            start_new_chunk(); /* first write of this chunk's life */
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
            /**
             * Advance, but leave the next chunk uncreated until there's
             * something to put in it -- same reasoning as begin(). A
             * capture that stops right on a chunk boundary shouldn't
             * evict anything on its way out.
             */
            next_chunk_seq_++;
            current_chunk_index_
                = static_cast<uint16_t>(next_chunk_seq_ % chunk_count_);
            chunk_open_ = false;
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
        chunk_open_          = true;

        /**
         * StorageFileMode::Write truncated this slot, so whatever chunk
         * previously occupied it is gone -- along with any capture whose
         * last chunk that was. This is the one and only point at which
         * data is actually evicted, so it's where pruning belongs.
         */
        prune_index();
    }

} // namespace core
