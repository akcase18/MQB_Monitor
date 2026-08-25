# core/logging/

Continuous signal-history recorder over the platform's `IStorage` (LittleFS_Program on internal Teensy flash, a host directory on the simulator — see [platform](../../platform/README.md)).

Recording is **opt-in**: nothing is written at all unless `Settings::logging_enabled` is set, toggled from [gui/screens](../../gui/screens/README.md)' `LoggingScreen` (swipe up from InfoScreen). Driving normally costs one bool test per sample tick; the user starts a capture when there's a drive worth keeping. Switching it off flushes whatever is buffered rather than stranding it, and drops every primed baseline so resuming re-primes instead of bridging the pause with a pre-pause value.

Chunks are created **lazily** — only by a flush that actually has records to write. That matters because creating a chunk truncates a ring slot and destroys whatever was there: if `begin()` created one per boot, a run of ignition cycles with the recorder off would chew through the ring and evict captures the user never touched. Booting, and stopping a capture exactly on a chunk boundary, both cost nothing.

Once enabled, two things scope what actually gets written:

- **Displayed signals only** — a record is only ever written for a signal currently assigned to a panel (`Config.priorities[0..num_panels-1]`, so at most `MAX_PANELS` = 6 of the catalog's 22). The log is a black-box trace of what the dashboard was actually showing. Every catalog signal is still polled over UDS regardless of what's on screen; this scopes what's *recorded*, and it's also what keeps write volume low enough for a useful retention window (see Sizing below).
- **On-change only** — a signal is only re-written when its value actually differs from what was last logged, so a gap for a given `signal_id` simply means it still held its last-logged value. Note this is a comparison of the *value*: `SignalValue::last_update_ms` advances on every UDS response whether or not the reading moved, so the timestamp alone can't tell a real change from a repeat.
- **Rate-floored at `LOG_MIN_INTERVAL_MS`** (40ms) — a signal arriving faster than that (the catalog's 20ms group) is recorded at 40ms spacing; a signal arriving slower (100ms, 500ms, …) is recorded at its own arrival cadence, untouched. It's a floor, not a target rate. Enforced per-signal inside `sample()` against its own last-logged timestamp rather than being implied by how often the caller ticks, so it holds regardless of `LOG_SAMPLE_PERIOD_MS` or scheduler jitter. Nothing in the catalog sits between 20ms and 100ms, so it coarsens exactly the group it's aimed at.

A signal newly assigned to a panel is **primed, not logged**: its first `sample()` captures whatever it currently holds as a baseline and writes nothing. Every catalog signal is polled whether or not it's displayed, so that value can be arbitrarily stale; the first record for a newly-displayed signal is the first genuinely new value to arrive afterwards.

## Files

- **`inc/log_record.hpp`** — on-disk formats, `#pragma pack`ed for a deterministic cross-platform byte layout (unlike `core::Config`, a chunk file is meant to be read back by a hand-ported struct on a third system — see `tools/log_export`):
  - `LogRecord { timestamp_ms, signal_id, value }` — 10 bytes. `timestamp_ms` is `SignalValue::last_update_ms`, boot-relative (no RTC exists anywhere in this codebase/hardware) — only meaningful alongside the owning chunk's `session_id`.
  - `LogIndexEntry { log_id, session_id, first_chunk_seq, last_chunk_seq, duration_ms }` plus `LogIndexHeader` — the capture index, see below.
  - `ChunkHeader { magic, version, session_id, chunk_seq }` — leads every chunk file. No record-count field: a reader derives how many records follow from how many bytes it actually receives past the header, since `IStorage` has no seek to go back and patch a count in after the fact once records have been appended across many flushes.

- **`inc/logger.hpp`** / **`src/logger.cpp`** — `Logger`:
  - Storage is a fixed ring of `CHUNK_COUNT` (52) whole files (`/log_000.bin` ..), each capped around `CHUNK_SIZE_BYTES` (256KB) — `IStorage` has no seek, so true in-place circular writes aren't possible; rotating through whole files (each opened `StorageFileMode::Write`, which creates/truncates from scratch, when its turn comes back around) is the shape that actually fits what `IStorage` offers.
  - `used_bytes()` reports how much of the ring holds data — completed chunks plus the open one's fill. It climbs to `ring_capacity_bytes()` and pins there once the ring laps, since rotation overwrites rather than frees; there is deliberately no "space remaining", which would be zero for the rest of the device's life.
  - `begin()` recovers rotation state purely by reading every existing chunk's header (skipping ones that don't exist or fail their magic/version check) — no separate cursor/counter file exists to get out of sync with the chunks themselves. `chunk_seq` is ever-increasing across the device's lifetime (never reset), so a reader can sort chunks into true chronological order after however many rotations have happened; `session_id` increments once per boot, so every chunk written during the same power cycle shares one value.
  - `sample()` — call once per tick (`LOG_SAMPLE_PERIOD_MS` in `src/main.cpp`, currently 20ms, matching the catalog's fastest arrival cadence so the 40ms floor lands cleanly instead of aliasing against a coarser tick). Walks the displayed panel set, buffers a record for each signal that clears both gates above, and flushes the RAM ring buffer to the current chunk (`StorageFileMode::Append`) once enough has accumulated or enough time has passed. It takes the live `Config` each call rather than latching the displayed set at `begin()`, so reassigning a panel takes effect on the log immediately with no separate "displayed set changed" notification to keep in sync. Two panels showing the same signal collapse to one record per change (the per-signal state is keyed by catalog index, not panel slot).

## The capture index (`/log_index.bin`)

A **capture** — one Logging on → off cycle — is what the user means by "a log". That is not the same as `session_id`, which only counts power cycles: three captures in one drive are three logs but one session.

Nothing in the chunk files can carry this. A chunk's header is written when the chunk is created, before it's known how many captures will start inside it, and `IStorage` has no seek to amend it afterwards. So `Logger` keeps a small side file: one `LogIndexEntry` per capture, recording which chunks hold it (`first_chunk_seq`/`last_chunk_seq`) and how long it ran.

**That chunk range is what makes eviction detectable.** The ring overwrites in `chunk_seq` order, so once the newest chunk has lapped more than `CHUNK_COUNT` past a capture's `last_chunk_seq`, every byte of it is gone — and `prune_index()` drops the entry. Pruning runs in `start_new_chunk()`, the one place data is actually destroyed, and again after `begin()`'s scan. A capture whose *earliest* chunks were overwritten but whose later ones survive still counts: part of it is still readable.

The index is a cache, not the source of truth — the chunk ring is. Every path reconciles against the ring, so a stale, truncated, or corrupt index degrades into an *incomplete* count rather than a wrong one, and a garbage file self-heals to empty exactly the way a corrupt chunk header does.

It's rewritten whole (~2.5KB) on capture start and stop, and otherwise at most every `INDEX_SAVE_INTERVAL_MS` (30s) while recording. A power cut mid-capture therefore costs at most 30s of accuracy in that capture's *recorded duration* — never any log data, which lives in the chunks.

Two caps worth knowing: `MAX_LOG_ENTRIES` (128) bounds how many captures can be tracked, and the ring's `CHUNK_COUNT` (52) bounds how many can survive — take more short captures than that and the oldest entries are dropped early, which is fine since they were next to be evicted anyway.

## Sizing

`CHUNK_COUNT * CHUNK_SIZE_BYTES` ≈ 13MB, sized against the worst case the two gates above allow: all 6 panels showing signals at the catalog's fastest 20ms cadence (`core/vehicle/src/signal_catalog.generated.inc`), every one of them genuinely changing every single arrival so on-change dedup saves nothing. `LOG_MIN_INTERVAL_MS` floors that at 40ms per signal, so the ceiling is 6 × 25 = 150 records/sec ≈ 1.5KB/s ≈ ~5.2MB/hour — **roughly 2.5 hours of guaranteed retention** within the 13MB budget.

**This budget is fixed, not derived from the backing store.** `CHUNK_COUNT * CHUNK_SIZE_BYTES` is decided at compile time and never consults `IStorage`, which is correct while onboard flash (~15.3MiB usable) is the only backend but wrong the moment a Micro-SD card is in play — a 128GB card would still see a 13MiB ring and leave the rest untouched. Tracked in [TODO.md](../../TODO.md); the sizing parameters are already constructor arguments, so it's mostly a matter of deriving them at boot from a capacity query on `IStorage`.

That's a hard floor on retention, and a pessimistic one. Only 5 catalog signals are at 20ms at all, so 6 panels can't all be in that group; a realistic panel set mixes in 100ms and 500ms signals, and on-change dedup then does real work on the steady ones (a coolant temp parked at 90 °C writes nothing at all). Actual retention in the car should run well past 2.5 hours. `platform/teensy/src/teensy_storage.cpp`'s `STORAGE_SIZE_BYTES` (14MiB) leaves headroom above the 13MiB ring for LittleFS's own block metadata and for firmware growth. (`core::Config` isn't in this region — `TeensyConfigStore` keeps it in EEPROM.) The hard ceiling is what `teensy_size` reports free for files, ~16.02MB ≈ 15.3MiB, which caps retention at this rate at **~3.0 hours** — so the storage lever is nearly exhausted, and past that the knobs are `LOG_MIN_INTERVAL_MS`, fewer panels, or a bigger/external chip.

**RAM note**: `Logger::BUFFER_CAPACITY` was originally sized for several seconds' worth of records, but on real hardware this pushed `teensymm`'s RAM1 "free for local variables" (effectively stack headroom) from ~63KB down under 15KB — most of that drop turned out to be a ~30KB fixed linker-layout cost from this module existing at all (present even with an 8-record buffer), not something that scales with `BUFFER_CAPACITY` itself. `BUFFER_CAPACITY` was shrunk to 512 (~3.5s worth at the worst case above) to recover real headroom (~25KB) without materially hurting flush batching, since the buffer filling up is now the usual flush trigger rather than `FLUSH_INTERVAL_MS`'s timer. Worth re-checking real hardware stack behavior if this area changes again.

That headroom figure is no longer the binding constraint it was: a later pass took `teensymm`'s "free for local variables" to ~87KB by fixing the LVGL draw buffers' element type (they were declared `lv_color_t`, a 3-byte struct, for a 2-byte-per-pixel RGB565 render buffer — see `src/main.cpp`) and by moving cold code out of RAM1 with `FLASHMEM` (see `platform/mem_placement.hpp`). Raising `BUFFER_CAPACITY` again is therefore affordable now if flush batching ever turns out to matter more than the RAM.

## Retrieval (Phase 1: USB only)

`src/main.cpp`'s `handle_log_command()` (Arduino/USB-CDC only, guarded by `#ifdef ARDUINO`) implements a small bespoke line command set over the same `Serial` the dashboard firmware already begins in `setup()`:

- `LOGDUMP` — streams every chunk (`open(Read)`, array-index order 0..`CHUNK_COUNT`-1, skipping ones that don't exist) as raw binary. The host tool reassembles true chronological order from each chunk's embedded `chunk_seq`/`session_id`, so the firmware doesn't need to replicate `begin()`'s ordering logic just to dump.
- `LOGCLEAR` — calls `Logger::erase()`: `IStorage::format()` (exactly what its own doc comment describes it for — reclaiming space once logs have been offloaded), then `begin()` against the empty result, plus dropping the in-RAM tail of any capture in progress so nothing buffered before the wipe survives into the first chunk after it. `LoggingScreen`'s Erase button makes the same call; the difference is that the on-screen one stops recording first, which this deliberately doesn't (see `handle_log_command()`).
- `LOGSTATUS` — a short text status line (ready/session/current chunk).

Deliberately blocking — this is meant to run as its own dedicated offload action (park, plug in, dump), not interleaved with driving.

Host side, [`tools/log_export`](../../tools/log_export/README.md) drives all three: `run.py` is a GUI (device status, download with a live byte count, session browse/preview, CSV export, erase) and `dump_log.py` the scriptable CLI, both decoding through one shared `mqb_logs/logfile.py`. Note what a dump *can't* carry: `/log_index.bin` isn't part of the `LOGDUMP` stream, so the host can group records by session but not by capture.

Wireless export (a dedicated device mode over the existing ESP32-C3 SoftAP link) and SD card (blocked on a Micro-SD slot that doesn't exist on the current PCB) are sequenced as later phases — see `.claude/plans` history for the fuller roadmap discussion. Both would reuse this same `LogRecord`/`ChunkHeader` on-disk format, just over a different transport.

## Usage

`src/main.cpp`'s `Composition` owns the one `Logger` instance (constructed against `platform.storage`), calls `begin()` once in `setup()` after `platform.storage.begin()`, and registers a single shared `scheduler.every()` task (not one per signal — `core::Scheduler::MAX_TASKS` is a fixed 32-slot table already carrying 22 signal-poll tasks plus a handful more) that calls `sample(signal_store, config_store.get(), clock.millis())` every `LOG_SAMPLE_PERIOD_MS`.
