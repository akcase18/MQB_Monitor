/**
 * @file log_record.hpp
 * @brief On-disk formats for Logger's rotating chunk files. Packed
 * (`#pragma pack`) rather than left to natural alignment like core::Config
 * is -- Config never leaves its own device (Teensy/simulator each keep a
 * separate persisted copy), but a chunk file is meant to be read back by a
 * hand-ported struct on a third system entirely (see tools/log_export), so
 * the wire layout has to be deterministic across compilers/architectures.
 */
#pragma once

#include <stdint.h>

namespace core
{

    #pragma pack(push, 1)

    /** One sampled signal value. Written only when a signal's value
     * actually changes (see Logger::sample()) -- a gap for a given
     * signal_id simply means it still held its last-logged value. */
    struct LogRecord
    {
            uint32_t timestamp_ms; /**
                                     * SignalValue::last_update_ms, boot-
                                     * relative -- no RTC exists in this
                                     * codebase/hardware, so only
                                     * meaningful alongside the owning
                                     * chunk's session_id. */
            uint16_t signal_id;    /* SignalSpec::id */
            float    value;
    };

    constexpr uint32_t LOG_MAGIC   = 0x4C4F4731; /* ASCII "LOG1" */
    constexpr uint8_t  LOG_VERSION = 1;

    /** Leads every chunk file. No record-count field -- a reader derives
     * how many records follow from how many bytes it actually receives
     * past the header (see Logger::dump()), which sidesteps needing to
     * patch this header after the fact once IStorage's lack of seek is
     * accounted for (records are appended over many flushes across a
     * chunk's lifetime, so the true count isn't known when this header is
     * first written). */
    struct ChunkHeader
    {
            uint32_t magic;      /* LOG_MAGIC */
            uint8_t  version;    /* LOG_VERSION */
            uint32_t session_id; /**
                                   * Increments once per boot -- every
                                   * chunk written during the same power
                                   * cycle shares one value, disambiguating
                                   * timestamp_ms across power cycles. */
            uint32_t chunk_seq;  /**
                                   * Ever-increasing across the device's
                                   * lifetime (never reset, never wraps
                                   * back to 0 the way the chunk's file-
                                   * name slot does) -- lets a reader sort
                                   * chunks into true chronological order
                                   * after however many rotations have
                                   * happened, since the file-index alone
                                   * (chunk_seq % CHUNK_COUNT) can't tell
                                   * two visits to the same slot apart. */
    };

    /**
     * ==================================================================
     * Log index -- a small side file (`/log_index.bin`) tracking one
     * entry per *capture* (a Logging on -> off cycle), which is what the
     * user thinks of as "a log". Nothing in the chunk files themselves
     * can carry this: a chunk's header is written when the chunk is
     * created, before it's known how many captures will start inside it,
     * and IStorage has no seek to go back and amend it. Several captures
     * can live in one chunk, and one capture can span many.
     *
     * The index is a cache, not the source of truth -- the chunk ring is.
     * Logger::begin() reconciles the two by dropping any entry whose
     * chunks have already been overwritten (see prune_index()), so a
     * stale or partially-written index self-heals into a merely
     * incomplete one rather than a wrong one.
     *=================================================================*/

    /** One capture. `first_chunk_seq`/`last_chunk_seq` bound which chunks
     * hold its records, which is what makes eviction detectable: once the
     * ring has lapped past `last_chunk_seq`, every byte of this capture
     * is gone and the entry goes with it. */
    struct LogIndexEntry
    {
            uint32_t log_id;
            uint32_t session_id; /**
                                   * Which boot -- pairs with the records'
                                   * boot-relative timestamp_ms. */
            uint32_t first_chunk_seq;
            uint32_t last_chunk_seq;
            uint32_t duration_ms; /* How long this capture ran. */
    };

    constexpr uint32_t LOG_INDEX_MAGIC   = 0x4C495831; /* ASCII "LIX1" */
    constexpr uint8_t  LOG_INDEX_VERSION = 1;

    struct LogIndexHeader
    {
            uint32_t magic;
            uint8_t  version;
            uint32_t next_log_id;
            uint16_t entry_count;
    };

    #pragma pack(pop)

    /** Path of the index file. Deliberately not matching `/log_%03u.bin`
     * so Logger::begin()'s chunk scan can't mistake it for a chunk. */
    constexpr const char *LOG_INDEX_PATH = "/log_index.bin";

    /** How many captures the index can track at once. Sized well past
     * what the ring can realistically hold distinct captures of, but it
     * is a cap: take more than this many very short captures and the
     * oldest entries are dropped early (they're the next to be evicted
     * anyway). 128 * 20 bytes = 2.5KB, in RAM and on disk. */
    constexpr uint16_t MAX_LOG_ENTRIES = 128;

} // namespace core
