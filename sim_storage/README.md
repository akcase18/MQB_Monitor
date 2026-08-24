# sim_storage/

Runtime storage for the desktop simulator. **Nothing in here is source** —
every file is written by a simulator run and is safe to delete at any time.
The directory is `.gitignore`d apart from this README.

This is `SimulatorStorage`'s backing directory
([platform/simulator/src/simulator_storage.cpp](../platform/simulator/src/simulator_storage.cpp)),
the host stand-in for `platform::IStorage`. On real hardware that interface
is backed by LittleFS on the Teensy's internal program flash; here it's just
a directory of files, since nothing exercising `IStorage` cares what the
medium actually is.

## What the files are

`log_000.bin`, `log_001.bin`, … are signal-history chunk files written by
[core/logging](../core/logging/README.md)'s `Logger` — the same on-disk
format the real firmware writes to flash, byte for byte, so anything that
parses one parses the other.

Each file is a 13-byte `ChunkHeader` (`magic` `"LOG1"`, `version`,
`session_id`, `chunk_seq`) followed by a packed stream of 10-byte
`LogRecord`s (`timestamp_ms`, `signal_id`, `value`). There's no record
count in the header — a reader derives it from the bytes that follow. See
[core/logging/inc/log_record.hpp](../core/logging/inc/log_record.hpp) for
the authoritative layout.

**You get one new file per simulator launch.** `Logger::begin()` always
starts a fresh chunk rather than resuming a partly-filled one, so
`session_id` and `chunk_seq` both increment once per run and the files
accumulate. On real hardware the ring wraps at `CHUNK_COUNT` (52) files and
overwrites the oldest; the simulator uses the same production sizing, so a
long enough run produces 256KB files — which is why this directory is
ignored rather than tracked.

Delete the whole directory to reset. The next run recreates it, restarting
at `log_000.bin` with `session_id` 1.

## Decoding a chunk locally

[tools/log_export/dump_log.py](../tools/log_export/dump_log.py) talks to
real hardware over USB serial, but its parser works on any buffer of this
format. Concatenating the chunk files gives exactly the byte stream
`LOGDUMP` would produce (the parser scans for chunk headers rather than
assuming boundaries), so they can be decoded to CSV without a Teensy
attached:

```sh
cat sim_storage/log_*.bin > /tmp/raw.bin
python3 -c "
import sys; sys.path.insert(0, 'tools/log_export')
from pathlib import Path
import dump_log as d
chunks = d.parse_chunks(Path('/tmp/raw.bin').read_bytes())
print(d.write_csv(chunks, Path('/tmp/sim_log.csv'), d.load_signal_names()), 'rows')
"
```

Records come out in true chronological order — `write_csv` sorts by
`chunk_seq`, which is what makes the ordering survive the ring wrapping
around on real hardware.

Note the *values* are the simulator's fabricated CAN responses, not
plausible vehicle data. This is useful for exercising the log format and
the export path, not for anything resembling real analysis.
