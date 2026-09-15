/**
 * @file logger.hpp
 * @brief Continuous signal-history recorder over IStorage. Records only the
 * signals currently assigned to panels (at most core::MAX_PANELS of the
 * catalog's SIGNAL_CATALOG_COUNT), not the whole catalog -- the log is a
 * black-box trace of what the dashboard was actually showing. (Every
 * catalog signal is still polled over UDS regardless of what's on screen;
 * this is a deliberate scoping of what gets *recorded*, which is also what
 * keeps write volume low enough for a useful retention window -- see
 * core/logging/README.md.) On-change only (see sample()) -- a gap for a
 * given signal
 * simply means it still held its last-logged value, so this loses nothing
 * relative to logging every poll, while using flash/write-cycles
 * proportional to how much is actually happening rather than a fixed tick
 * rate.
 *
 * Storage is a fixed ring of CHUNK_COUNT whole files (`/log_000.bin` ..),
 * each capped around CHUNK_SIZE_BYTES -- IStorage has no seek, so true
 * in-place circular writes aren't possible; rotating through whole files
 * instead (each opened StorageFileMode::Write, which creates/truncates from
 * scratch, when its turn comes back around) is the shape that actually fits
 * what IStorage offers. Within one chunk's lifetime, records are appended
 * across many flushes (StorageFileMode::Append) rather than buffered
 * entirely in RAM first -- the buffer only needs to hold a few seconds'
 * worth of records at a time, not a whole chunk.
 *
 * begin() recovers rotation state (which chunk to write next, and this
 * boot's session_id) purely by reading every existing chunk's header --
 * see its doc comment. No separate cursor/counter file exists to get out of
 * sync with the chunks themselves.
 */
#pragma once

#include "config.hpp"
#include "log_record.hpp"
#include "platform.hpp"
#include "signal_catalog.hpp"
#include "signal_store.hpp"

namespace core
{

    /**
     * Sized for roughly a few hours of retention in the worst case --
     * all MAX_PANELS panels showing 20ms-cadence signals, every one of
     * them genuinely changing every sample -- against the dashboard's
     * real per-signal cadences (see core/logging/README.md for the
     * derivation) once the fastest signals are logged at
     * LOG_SAMPLE_PERIOD_MS (see src/main.cpp) rather than their raw 50ms
     * display cadence. Total budget: CHUNK_COUNT * CHUNK_SIZE_BYTES ==
     * 13MiB, inside the 14MiB reserved for IStorage on the Teensy backend
     *
     * Fixed at compile time and never derived from the backing store,
     * which is right for onboard flash but leaves a Micro-SD card almost
     * entirely unused -- see TODO.md. Both sizing parameters are already
     * constructor arguments, so deriving them at boot from a capacity
     * query on IStorage is the intended fix.
     *
     * (see teensy_storage.cpp's STORAGE_SIZE_BYTES), leaving headroom for
     * LittleFS's own block metadata and firmware growth. core::Config is
     * not in that region -- TeensyConfigStore keeps it in EEPROM. */
    constexpr uint32_t CHUNK_SIZE_BYTES = 256u * 1024u;
    constexpr uint16_t CHUNK_COUNT      = 52;

    /**
     * Floor on how often any one signal may be recorded. A signal
     * arriving faster than this (the catalog's 20ms group) is logged at
     * 40ms spacing; a signal arriving slower (100ms, 500ms, ...) is
     * logged at its own arrival cadence, untouched -- this is a floor,
     * not a target rate. Enforced inside sample() against each signal's
     * own last-logged time rather than being implied by how often the
     * caller happens to tick, so the guarantee holds regardless of
     * LOG_SAMPLE_PERIOD_MS (see src/main.cpp) or scheduler jitter.
     *
     * Nothing in the catalog sits between 20ms and 100ms
     * (signal_catalog.generated.inc), so this coarsens exactly the 20ms
     * group it's meant to and leaves every other signal alone. */
    constexpr uint32_t LOG_MIN_INTERVAL_MS = 50;  /**
                                                   * was 40 before the
                                                   * 2026-08-25 cadence
                                                   * floor -- see below */

    /** Writes `path` as `/log_%03u.bin` for `chunk_index` (0..CHUNK_COUNT-1)
     * into `out` (must be at least CHUNK_PATH_LEN bytes) -- shared between
     * Logger's own rotation and any external reader (see src/main.cpp's
     * LOGDUMP handler) so the naming convention lives in exactly one
     * place. */
    constexpr size_t CHUNK_PATH_LEN = 16;
    void              chunk_path(uint16_t chunk_index, char *out);

    class Logger
    {
        public:
            /** `chunk_size_bytes`/`chunk_count` default to the production
             * sizing (CHUNK_SIZE_BYTES/CHUNK_COUNT above) -- overridable
             * purely so tests can exercise rotation/wraparound without
             * needing to write tens of megabytes per test case (see
             * test/test_core_logging). */
            explicit Logger(IStorage &backing,
                            uint32_t  chunk_size_bytes = CHUNK_SIZE_BYTES,
                            uint16_t  chunk_count       = CHUNK_COUNT);

            /**
             * Call after `backing`'s own begin() (the composition root
             * owns that call, same as it does for platform.storage
             * elsewhere -- see src/main.cpp). Recovers rotation state:
             * scans every
             * `/log_%03u.bin` slot's header (skipping ones that don't
             * exist or fail their magic/version check -- self-heals from a
             * blank or partially-corrupt filesystem with no special-casing
             * needed), takes the highest chunk_seq/session_id found across
             * all of them, and starts a fresh chunk one slot past it with
             * session_id one past the highest seen. A chunk always starts
             * fresh on boot rather than resuming a previous session's
             * partially-filled one -- IStorage exposes no file size to
             * know how full it already was, and this keeps each chunk's
             * contents within exactly one session_id.
             *
             * @returns True on success. False leaves the logger inert --
             * sample() becomes a no-op rather than crashing if storage
             * isn't available.
             */
            bool begin();

            /**
             * Throws away every recorded log and starts over: formats the
             * backing store, then re-derives rotation state from the (now
             * empty) result exactly as begin() does at boot.
             *
             * Also drops the in-RAM side of a capture -- the unflushed
             * record buffer, the primed baselines, and the was-enabled
             * latch. Without that, a still-buffered record from before
             * the erase would land in the first chunk written after it
             * (surviving the wipe), and the next sample() tick would
             * close a capture that no longer has an index entry. Every
             * signal therefore re-primes, the same as it does when
             * logging is switched off and back on.
             *
             * Recording itself is not stopped -- Settings::logging_enabled
             * lives in core::Config, which this class deliberately doesn't
             * own. A caller that wants recording off (both retrieval paths
             * do) clears it first; if it doesn't, sample() simply opens a
             * fresh capture on its next tick.
             *
             * @returns True on success. False on a store that won't
             * format, leaving the logger inert exactly as a failed
             * begin() does.
             */
            bool erase();

            /**
             * Call once per sample tick (see LOG_SAMPLE_PERIOD_MS in
             * src/main.cpp). Buffers a record for each currently-displayed
             * signal (`config.priorities[0..settings.num_panels-1]`, so at
             * most MAX_PANELS of them) whose SignalValue::last_update_ms
             * has advanced since it was last sampled, then flushes the
             * buffer to the current chunk if enough has accumulated or
             * enough time has passed since the last flush.
             *
             * `config` is re-read every tick rather than latched at
             * begin() so that reassigning a panel (or changing the panel
             * count) takes effect on the log immediately, with no separate
             * "the displayed set changed" notification to keep in sync.
             * A signal newly brought on screen is *primed* on the first
             * tick after the change -- its current value is captured as a
             * baseline but not written -- so a value that had been
             * sitting in SignalStore since before it was displayed never
             * lands in the log. The first record for it is the first
             * genuinely new value to arrive afterwards.
             *
             * Does nothing at all unless Settings::logging_enabled is
             * set -- logging is opt-in (see config.hpp). Switching it off
             * flushes whatever was buffered and drops every primed
             * baseline, so resuming re-primes rather than bridging the
             * pause with a pre-pause value.
             */
            void sample(const SignalStore &signals,
                        const Config      &config,
                        uint32_t           now_ms);

            /** Read-only status for a USB/wireless retrieval command to
             * report back (see src/main.cpp's LOGSTATUS handler) --
             * doesn't affect capture/rotation in any way. */
            bool     is_ready() const
            {
                return ready_;
            }
            uint32_t session_id() const
            {
                return session_id_;
            }
            uint16_t current_chunk_index() const
            {
                return current_chunk_index_;
            }

            /**
             * ==========================================================
             * Log-index status, for LoggingScreen. All in-RAM reads --
             * the index is loaded once by begin() and kept current from
             * there, so a screen can poll these every refresh without
             * touching storage.
             *=========================================================*/

            /** How many captures still have at least some data in the
             * ring. Drops as the ring laps past old captures -- see
             * log_record.hpp's LogIndexEntry. A capture whose earliest
             * chunks have been overwritten but whose later ones survive
             * still counts: part of it is still readable. */
            uint16_t log_count() const
            {
                return entry_count_;
            }

            /** Summed duration of every capture still counted above.
             * Excludes captures already evicted, so it tracks what's
             * actually retrievable rather than what was ever recorded. */
            uint32_t total_logged_ms() const;

            /** True between the tick that saw logging switch on and the
             * one that saw it switch off -- i.e. a capture is open and
             * accruing into the newest index entry. */
            bool is_capturing() const
            {
                return was_enabled_;
            }

            /** Total bytes the chunk ring can hold, this Logger's actual
             * configured sizing rather than the CHUNK_* defaults (tests
             * override them). */
            uint64_t ring_capacity_bytes() const
            {
                return static_cast<uint64_t>(chunk_size_bytes_)
                       * static_cast<uint64_t>(chunk_count_);
            }

            /**
             * Bytes of the ring currently holding log data.
             *
             * Rises toward ring_capacity_bytes() and then stays pinned
             * there: once the ring has lapped, every slot holds data
             * forever after, because rotation overwrites rather than
             * frees. So this answers "how much of the ring is in use",
             * never "how much room is left" -- past the first lap there
             * is no room left and recording continues anyway, dropping
             * the oldest chunk each time around.
             */
            uint64_t used_bytes() const;

        private:
            /**
             * ~3.5s worth of records at worst-case write rate -- in
             * practice the buffer filling up is the usual flush trigger
             * (not FLUSH_INTERVAL_MS's timer), trading a bit more flash-
             * write frequency for keeping this comfortably small in
             * static RAM. Deliberately not the several-seconds size
             * originally sized for: on real hardware this pushed
             * RAM1's "free for local variables" (effectively stack
             * headroom) from 63KB down under 15KB (see the note in
             * core/logging/README.md), most of which turned out to be a
             * ~30KB fixed linker-layout cost from this module existing at
             * all rather than scaling with BUFFER_CAPACITY -- so a
             * smaller buffer recovers real headroom without meaningfully
             * hurting flush batching. */
            static constexpr uint16_t BUFFER_CAPACITY    = 512;
            static constexpr uint32_t FLUSH_INTERVAL_MS  = 5000;

            void flush();
            void start_new_chunk();

            /** Reads `/log_index.bin` into entries_, or starts an empty
             * index if it's missing/corrupt. */
            void load_index();
            void save_index();
            /** Drops entries the ring has already lapped past. Called
             * after begin()'s scan and again on every rotation, since
             * eviction happens as chunks are reused, not only at boot. */
            void prune_index();
            /** chunk_seq of the oldest chunk still in the ring. */
            uint32_t oldest_live_chunk_seq() const;
            void     begin_capture(uint32_t now_ms);
            void     end_capture(uint32_t now_ms);

            IStorage &backing_;
            uint32_t  chunk_size_bytes_;
            uint16_t  chunk_count_;
            bool      ready_ = false;

            LogRecord buffer_[BUFFER_CAPACITY];
            uint16_t  buffer_count_ = 0;

            /**
             * Per-catalog-index (not per signal_id -- ids aren't
             * contiguous, see signal_catalog.generated.inc) state behind
             * sample()'s decision to write a record or not.
             *
             * last_seen_ms_/last_value_ are the timestamp and value of the
             * last record actually written for that signal -- the former
             * enforces LOG_MIN_INTERVAL_MS, the latter is what makes this
             * on-*change* rather than on-poll (SignalValue::last_update_ms
             * advances on every UDS response whether or not the value
             * moved, so the timestamp alone can't tell them apart).
             * last_value_ starts as NaN, which compares unequal to
             * everything, so a signal's first real value always writes.
             *
             * in_display_set_ is whether the signal was on a panel during
             * the previous sample() call -- see sample()'s doc comment on
             * priming. */
            uint32_t last_seen_ms_[SIGNAL_CATALOG_COUNT]   = {};
            float    last_value_[SIGNAL_CATALOG_COUNT];
            bool     in_display_set_[SIGNAL_CATALOG_COUNT] = {};

            /**
             * Whether Settings::logging_enabled was set on the previous
             * sample() call -- lets the off-transition be caught once
             * (flush what's buffered, drop the primed baselines) instead
             * of every tick while logging stays off. */
            bool was_enabled_ = false;

            /**
             * Log index -- see log_record.hpp. entries_ is ordered oldest
             * capture first, so pruning is always a front-truncation. */
            LogIndexEntry entries_[MAX_LOG_ENTRIES] = {};
            uint16_t      entry_count_              = 0;
            uint32_t      next_log_id_              = 1;
            /**
             * Boot-relative start of the capture currently open, so its
             * duration can be recomputed each tick rather than
             * accumulated (which would drift). */
            uint32_t active_capture_start_ms_ = 0;
            uint32_t last_index_save_ms_      = 0;

            /**
             * The index is rewritten whole (it's only ~2.5KB), so it's
             * saved on capture start/stop and otherwise only this often
             * -- a power cut mid-capture then costs at most this much
             * accuracy in that capture's recorded duration, never any
             * actual log data, which lives in the chunks. */
            static constexpr uint32_t INDEX_SAVE_INTERVAL_MS = 30000;

            uint32_t session_id_          = 1;
            uint32_t next_chunk_seq_      = 0;
            uint16_t current_chunk_index_ = 0;
            uint32_t chunk_bytes_written_ = 0;
            /**
             * Whether the chunk at current_chunk_index_ has actually been
             * created on disk yet. Chunks are created lazily, on the
             * first flush that has something to put in them, so that
             * neither booting nor stopping a capture on a chunk boundary
             * costs a ring slot -- see begin(). */
            bool     chunk_open_          = false;
            uint32_t last_flush_ms_       = 0;
    };

} // namespace core
