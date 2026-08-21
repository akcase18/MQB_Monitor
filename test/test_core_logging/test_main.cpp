/**
 * @file test_main.cpp
 * @brief Host-buildable coverage for core/logging: boot recovery on blank
 * storage, on-change dedup, scoping to the displayed panel set, flush
 * triggering, chunk rotation/wraparound, and self-healing recovery from a
 * corrupt/partial filesystem. Chunk sizing is
 * overridden to tiny values via Logger's test-only constructor args (see
 * logger.hpp) -- the real ~13MB production budget would make a wraparound
 * test write tens of megabytes per run. Run with `pio test -e simulator`.
 */
#include <string.h>
#include <unity.h>

#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include "logger.hpp"
#include "signal_store.hpp"

namespace
{
    /* In-memory IStorage test double -- a plain path -> byte-vector map, so
     * tests stay hermetic (no touching the real simulator's sim_storage/
     * directory) and can read back exactly what Logger wrote via the same
     * public open/read/write/close API a real caller would use. */
    class FakeStorage : public IStorage
    {
        public:
            bool begin() override
            {
                present_ = true;
                return true;
            }

            bool is_present() override
            {
                return present_;
            }

            bool format() override
            {
                if (open_)
                {
                    return false;
                }
                files_.clear();
                return true;
            }

            bool open(const char *path, StorageFileMode mode) override
            {
                if (open_ || !present_)
                {
                    return false;
                }

                std::string p = path;
                if (mode == StorageFileMode::Read)
                {
                    if (files_.find(p) == files_.end())
                    {
                        return false;
                    }
                }
                else if (mode == StorageFileMode::Write)
                {
                    files_[p].clear();
                }
                else /* Append -- create if missing, else keep contents */
                {
                    files_[p]; /* ensures an entry exists */
                }

                current_  = p;
                mode_     = mode;
                read_pos_ = 0;
                open_     = true;
                return true;
            }

            bool is_open() const override
            {
                return open_;
            }

            void close() override
            {
                open_ = false;
                current_.clear();
            }

            size_t read(void *buf, size_t len) override
            {
                if (!open_ || mode_ != StorageFileMode::Read)
                {
                    return 0;
                }
                std::vector<uint8_t> &data  = files_[current_];
                size_t                avail = data.size() - read_pos_;
                size_t                n     = len < avail ? len : avail;
                memcpy(buf, data.data() + read_pos_, n);
                read_pos_ += n;
                return n;
            }

            size_t write(const void *buf, size_t len) override
            {
                if (!open_ || mode_ == StorageFileMode::Read)
                {
                    return 0;
                }
                std::vector<uint8_t> &data = files_[current_];
                const uint8_t *p = static_cast<const uint8_t *>(buf);
                data.insert(data.end(), p, p + len);
                return len;
            }

            bool exists(const char *path) const override
            {
                return files_.find(path) != files_.end();
            }

            bool remove(const char *path) override
            {
                return files_.erase(path) > 0;
            }

            /** Test-only helper: raw contents of `path`, or an empty vector
             * if it was never written. Reads via the map directly rather
             * than the open/read/close API, purely so tests can assert on
             * a chunk's full contents in one line. */
            std::vector<uint8_t> file_bytes(const std::string &path) const
            {
                auto it = files_.find(path);
                return it == files_.end() ? std::vector<uint8_t> {}
                                          : it->second;
            }

        private:
            bool                                          present_ = false;
            bool                                           open_   = false;
            StorageFileMode                                mode_ = StorageFileMode::Read;
            std::string                                    current_;
            size_t                                         read_pos_ = 0;
            std::map<std::string, std::vector<uint8_t>>    files_;
    };

    std::string chunk_path_str(uint16_t index)
    {
        char path[core::CHUNK_PATH_LEN];
        core::chunk_path(index, path);
        return path;
    }

    /** A Config with `ids` assigned to panels, in order -- the only part of
     * Config Logger::sample() reads (see its doc comment). Everything else
     * is left zeroed. */
    core::Config displaying(std::initializer_list<uint16_t> ids)
    {
        core::Config cfg {};
        cfg.settings.num_panels = static_cast<uint8_t>(ids.size());

        uint8_t i = 0;
        for (uint16_t id : ids)
        {
            cfg.priorities[i++].signal_id = id;
        }
        return cfg;
    }
} // namespace

void setUp()
{
}
void tearDown()
{
}

void test_begin_starts_fresh_chunk_on_blank_storage()
{
    FakeStorage storage;
    storage.begin();

    core::Logger logger(storage);
    TEST_ASSERT_TRUE(logger.begin());
    TEST_ASSERT_TRUE(logger.is_ready());
    TEST_ASSERT_EQUAL_UINT32(1, logger.session_id());
    TEST_ASSERT_EQUAL_UINT16(0, logger.current_chunk_index());

    std::vector<uint8_t> bytes = storage.file_bytes(chunk_path_str(0));
    TEST_ASSERT_EQUAL_size_t(sizeof(core::ChunkHeader), bytes.size());

    core::ChunkHeader header;
    memcpy(&header, bytes.data(), sizeof(header));
    TEST_ASSERT_EQUAL_UINT32(core::LOG_MAGIC, header.magic);
    TEST_ASSERT_EQUAL_UINT8(core::LOG_VERSION, header.version);
    TEST_ASSERT_EQUAL_UINT32(1, header.session_id);
    TEST_ASSERT_EQUAL_UINT32(0, header.chunk_seq);
}

void test_sample_records_a_changed_value_but_not_a_repeated_one()
{
    FakeStorage storage;
    storage.begin();
    core::Logger logger(storage);
    logger.begin();

    core::SignalStore signals;
    uint16_t           id  = core::SIGNAL_CATALOG[0].id;
    core::Config       cfg = displaying({ id });

    signals.set(id, 42.0f, 100);
    logger.sample(signals, cfg, 100); // primes the baseline, writes nothing

    /* Polled again, far enough past the rate floor, but the value hasn't
     * moved -- SignalValue::last_update_ms advanced anyway, so this is
     * what separates on-change from on-poll. */
    signals.set(id, 42.0f, 200);
    logger.sample(signals, cfg, 200);

    signals.set(id, 55.0f, 300); // a real change
    logger.sample(signals, cfg, 300);

    logger.sample(signals, cfg, 5300); // due (>=5000ms) -- flush

    std::vector<uint8_t> bytes = storage.file_bytes(chunk_path_str(0));
    TEST_ASSERT_EQUAL_size_t(
        sizeof(core::ChunkHeader) + sizeof(core::LogRecord), bytes.size());

    core::LogRecord record;
    memcpy(&record,
          bytes.data() + sizeof(core::ChunkHeader),
          sizeof(record));
    TEST_ASSERT_EQUAL_UINT32(300, record.timestamp_ms);
    TEST_ASSERT_EQUAL_UINT16(id, record.signal_id);
    TEST_ASSERT_EQUAL_FLOAT(55.0f, record.value);
}

void test_a_fast_signal_is_rate_floored_to_the_minimum_interval()
{
    FakeStorage storage;
    storage.begin();
    core::Logger logger(storage);
    logger.begin();

    core::SignalStore signals;
    uint16_t           id  = core::SIGNAL_CATALOG[0].id;
    core::Config       cfg = displaying({ id });

    /* A 20ms-cadence signal with a genuinely new value every arrival --
     * the case LOG_MIN_INTERVAL_MS exists for. Sampled every 20ms too, so
     * nothing but the floor itself is limiting the rate. */
    signals.set(id, 0.0f, 0);
    logger.sample(signals, cfg, 0); // primes

    for (uint32_t t = 20; t <= 200; t += 20)
    {
        signals.set(id, static_cast<float>(t), t);
        logger.sample(signals, cfg, t);
    }

    logger.sample(signals, cfg, 5200); // flush

    std::vector<uint8_t> bytes = storage.file_bytes(chunk_path_str(0));
    TEST_ASSERT_EQUAL_size_t(
        sizeof(core::ChunkHeader) + 5 * sizeof(core::LogRecord),
        bytes.size());

    /* Every other arrival, i.e. 40ms apart -- not the 20ms it arrived at,
     * and not coarser. */
    core::LogRecord records[5];
    memcpy(records, bytes.data() + sizeof(core::ChunkHeader),
           sizeof(records));
    for (uint32_t i = 0; i < 5; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(40 * (i + 1), records[i].timestamp_ms);
    }
}

void test_a_slower_signal_is_logged_at_its_own_arrival_cadence()
{
    FakeStorage storage;
    storage.begin();
    core::Logger logger(storage);
    logger.begin();

    core::SignalStore signals;
    uint16_t           id  = core::SIGNAL_CATALOG[0].id;
    core::Config       cfg = displaying({ id });

    signals.set(id, 0.0f, 0);
    logger.sample(signals, cfg, 0); // primes

    /* 100ms arrivals, sampled at 20ms. The floor is a floor, not a target
     * rate: every arrival past it gets its own record, so this signal is
     * logged at 100ms rather than being pulled to 40ms. */
    for (uint32_t t = 20; t <= 500; t += 20)
    {
        if (t % 100 == 0)
        {
            signals.set(id, static_cast<float>(t), t);
        }
        logger.sample(signals, cfg, t);
    }

    logger.sample(signals, cfg, 5500); // flush

    std::vector<uint8_t> bytes = storage.file_bytes(chunk_path_str(0));
    TEST_ASSERT_EQUAL_size_t(
        sizeof(core::ChunkHeader) + 5 * sizeof(core::LogRecord),
        bytes.size());

    core::LogRecord records[5];
    memcpy(records, bytes.data() + sizeof(core::ChunkHeader),
           sizeof(records));
    for (uint32_t i = 0; i < 5; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(100 * (i + 1), records[i].timestamp_ms);
    }
}

void test_sample_before_begin_or_without_data_is_a_no_op()
{
    FakeStorage        storage;
    storage.begin();
    core::Logger       logger(storage); // begin() never called
    core::SignalStore  signals;
    uint16_t           id  = core::SIGNAL_CATALOG[0].id;
    core::Config       cfg = displaying({ id });
    signals.set(id, 1.0f, 100);

    logger.sample(signals, cfg, 100);
    TEST_ASSERT_FALSE(logger.is_ready());
    TEST_ASSERT_FALSE(storage.exists(chunk_path_str(0).c_str()));
}

void test_sample_records_only_the_signals_currently_on_screen()
{
    FakeStorage storage;
    storage.begin();
    core::Logger logger(storage);
    logger.begin();

    /* Two signals both have fresh data, but only the first is assigned to
     * a panel -- the second must not reach the log at all. */
    core::SignalStore signals;
    uint16_t           shown  = core::SIGNAL_CATALOG[0].id;
    uint16_t           hidden = core::SIGNAL_CATALOG[1].id;
    signals.set(shown, 11.0f, 100);
    signals.set(hidden, 22.0f, 100);

    core::Config cfg = displaying({ shown });
    logger.sample(signals, cfg, 100); // primes `shown`

    /* Both keep updating -- only the one on a panel may be recorded. */
    signals.set(shown, 12.0f, 200);
    signals.set(hidden, 23.0f, 200);
    logger.sample(signals, cfg, 200);

    logger.sample(signals, cfg, 5200); // due -- flush

    std::vector<uint8_t> bytes = storage.file_bytes(chunk_path_str(0));
    TEST_ASSERT_EQUAL_size_t(
        sizeof(core::ChunkHeader) + sizeof(core::LogRecord), bytes.size());

    core::LogRecord record;
    memcpy(&record,
           bytes.data() + sizeof(core::ChunkHeader),
           sizeof(record));
    TEST_ASSERT_EQUAL_UINT16(shown, record.signal_id);
}

void test_a_reassigned_panels_new_signal_is_primed_not_logged_stale()
{
    FakeStorage storage;
    storage.begin();
    core::Logger logger(storage);
    logger.begin();

    core::SignalStore signals;
    uint16_t           first  = core::SIGNAL_CATALOG[0].id;
    uint16_t           second = core::SIGNAL_CATALOG[1].id;

    /* `second` has been polled all along despite never being displayed,
     * so by the time it goes on screen it already holds a value from
     * t=100 that must never reach the log. */
    signals.set(first, 11.0f, 100);
    signals.set(second, 22.0f, 100);

    core::Config cfg = displaying({ first });
    logger.sample(signals, cfg, 100); // primes `first`

    signals.set(first, 12.0f, 200);
    logger.sample(signals, cfg, 200); // records `first` = 12.0

    /* User reassigns the panel to `second`. Its stale 22.0 from t=100 is
     * taken as a baseline only -- no record. */
    cfg.priorities[0].signal_id = second;
    logger.sample(signals, cfg, 240);
    logger.sample(signals, cfg, 300);

    signals.set(second, 33.0f, 400); // first genuinely new value
    logger.sample(signals, cfg, 400);

    logger.sample(signals, cfg, 5400); // due -- flush

    std::vector<uint8_t> bytes = storage.file_bytes(chunk_path_str(0));
    TEST_ASSERT_EQUAL_size_t(
        sizeof(core::ChunkHeader) + 2 * sizeof(core::LogRecord),
        bytes.size());

    core::LogRecord records[2];
    memcpy(records,
           bytes.data() + sizeof(core::ChunkHeader),
           sizeof(records));
    TEST_ASSERT_EQUAL_UINT16(first, records[0].signal_id);
    TEST_ASSERT_EQUAL_FLOAT(12.0f, records[0].value);

    /* The stale 22.0 is absent entirely -- the first record for `second`
     * is the value that arrived after it came on screen. */
    TEST_ASSERT_EQUAL_UINT16(second, records[1].signal_id);
    TEST_ASSERT_EQUAL_UINT32(400, records[1].timestamp_ms);
    TEST_ASSERT_EQUAL_FLOAT(33.0f, records[1].value);
}

void test_chunk_rotation_advances_through_all_slots_then_wraps()
{
    FakeStorage  storage;
    storage.begin();

    /* One record (10 bytes) per chunk before it rotates -- tiny on
     * purpose, see the file doc comment. */
    core::Logger logger(
        storage, sizeof(core::ChunkHeader) + sizeof(core::LogRecord), 3);
    logger.begin();
    TEST_ASSERT_EQUAL_UINT16(0, logger.current_chunk_index());

    core::SignalStore signals;
    uint16_t           id  = core::SIGNAL_CATALOG[0].id;
    core::Config       cfg = displaying({ id });

    /* The first sample after a signal comes on screen only primes its
     * baseline (see Logger::sample), so get that out of the way up front
     * and leave each loop iteration below a clean one-record-one-rotation
     * step. */
    signals.set(id, -1.0f, 100);
    logger.sample(signals, cfg, 100);

    /* Each iteration: one changed value (forces a buffered record) then a
     * big enough time jump to guarantee a flush -- fills chunk i to
     * capacity, rotating into chunk i+1. */
    for (uint32_t i = 0; i < 5; i++)
    {
        signals.set(id, static_cast<float>(i), 1000 + i * 10000);
        logger.sample(signals, cfg, 1000 + i * 10000);
        logger.sample(signals, cfg, 1000 + i * 10000 + 6000); // due -> flush
    }

    /* begin() writes chunk_seq 0 into slot 0; the 5 loop iterations each
     * fill their chunk to capacity and rotate, writing chunk_seq
     * 1,2,3,4,5 into slots 1,2,0,1,2 in turn (each overwriting that
     * slot's earlier visit) -- so the ring ends up mid-way through its
     * second full lap, freshly started on slot 2 (chunk_seq 5, header
     * only, no record yet). */
    TEST_ASSERT_EQUAL_UINT16(2, logger.current_chunk_index());

    /* Slot 0 was overwritten by chunk_seq 3 -- confirm it holds the
     * *newer* record (value 3.0), not the original chunk_seq 0 (value
     * 0.0) -- the whole point of wraparound being an overwrite, not an
     * append. */
    std::vector<uint8_t> slot0 = storage.file_bytes(chunk_path_str(0));
    TEST_ASSERT_EQUAL_size_t(
        sizeof(core::ChunkHeader) + sizeof(core::LogRecord), slot0.size());

    core::ChunkHeader header;
    memcpy(&header, slot0.data(), sizeof(header));
    TEST_ASSERT_EQUAL_UINT32(3, header.chunk_seq);

    core::LogRecord record;
    memcpy(&record, slot0.data() + sizeof(header), sizeof(record));
    TEST_ASSERT_EQUAL_FLOAT(3.0f, record.value);
}

void test_begin_self_heals_from_corrupt_chunk_and_recovers_max_seq()
{
    FakeStorage storage;
    storage.begin();

    /* Slot 0: a valid header (chunk_seq=0, session_id=1) -- large
     * chunk_size_bytes so this never rotates, keeping the scenario to
     * exactly one real chunk. */
    core::Logger seed(storage, 4096, 4);
    TEST_ASSERT_TRUE(seed.begin());

    /* Slot 3 gets garbage instead of a real header -- begin() must skip
     * it (wrong/missing magic) rather than misreading it as a
     * higher-priority high-water mark than slot 0's real one. */
    storage.open(chunk_path_str(3).c_str(), StorageFileMode::Write);
    uint8_t garbage[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01 };
    storage.write(garbage, sizeof(garbage));
    storage.close();

    core::Logger recovered(storage, 4096, 4);
    TEST_ASSERT_TRUE(recovered.begin());

    /* Recovers exactly seed's chunk_seq=0/session_id=1 from slot 0 and
     * starts strictly past both -- next chunk_seq=1 (slot 1), session
     * incremented to 2. */
    TEST_ASSERT_EQUAL_UINT32(2, recovered.session_id());
    TEST_ASSERT_EQUAL_UINT16(1, recovered.current_chunk_index());
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_begin_starts_fresh_chunk_on_blank_storage);
    RUN_TEST(test_sample_records_a_changed_value_but_not_a_repeated_one);
    RUN_TEST(test_a_fast_signal_is_rate_floored_to_the_minimum_interval);
    RUN_TEST(test_a_slower_signal_is_logged_at_its_own_arrival_cadence);
    RUN_TEST(test_sample_before_begin_or_without_data_is_a_no_op);
    RUN_TEST(test_sample_records_only_the_signals_currently_on_screen);
    RUN_TEST(test_a_reassigned_panels_new_signal_is_primed_not_logged_stale);
    RUN_TEST(test_chunk_rotation_advances_through_all_slots_then_wraps);
    RUN_TEST(test_begin_self_heals_from_corrupt_chunk_and_recovers_max_seq);
    return UNITY_END();
}
