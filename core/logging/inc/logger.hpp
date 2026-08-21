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

    /* Sized for roughly a few hours of retention in the worst case --
     * all MAX_PANELS panels showing 20ms-cadence signals, every one of
     * them genuinely changing every sample -- against the dashboard's
     * real per-signal cadences (see core/logging/README.md for the
     * derivation) once the fastest signals are logged at
     * LOG_SAMPLE_PERIOD_MS (see src/main.cpp) rather than their raw 20ms
     * display cadence. Total budget: CHUNK_COUNT * CHUNK_SIZE_BYTES ==
     * ~13MB, comfortably inside the ~14MB now reserved for IStorage on the
     * Teensy backend (see teensy_storage.cpp's STORAGE_SIZE_BYTES) with
     * headroom left for core::Config and filesystem overhead. */
    constexpr uint32_t CHUNK_SIZE_BYTES = 256u * 1024u;
    constexpr uint16_t CHUNK_COUNT      = 52;

    /* Floor on how often any one signal may be recorded. A signal
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
    constexpr uint32_t LOG_MIN_INTERVAL_MS = 40;

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

        private:
            /* ~3.5s worth of records at worst-case write rate -- in
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

            IStorage &backing_;
            uint32_t  chunk_size_bytes_;
            uint16_t  chunk_count_;
            bool      ready_ = false;

            LogRecord buffer_[BUFFER_CAPACITY];
            uint16_t  buffer_count_ = 0;

            /* Per-catalog-index (not per signal_id -- ids aren't
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

            uint32_t session_id_          = 1;
            uint32_t next_chunk_seq_      = 0;
            uint16_t current_chunk_index_ = 0;
            uint32_t chunk_bytes_written_ = 0;
            uint32_t last_flush_ms_       = 0;
    };

} // namespace core
