# tools/log_export/

Pulls the dashboard firmware's black-box signal log (see [core/logging](../../core/logging/README.md)) over USB serial and decodes it to CSV.

- **[`dump_log.py`](dump_log.py)** — talks a small bespoke command set (`LOGDUMP`/`LOGSTATUS`/`LOGCLEAR`) to `env:teensymm`'s own USB-CDC handler (`src/main.cpp`'s `handle_log_command()`). **Not** the SLCAN protocol `env:can_sniffer`/`tools/monitor` speak — that's a completely separate board/firmware/protocol for live CAN sniffing, unrelated to this.

```sh
python3 tools/log_export/dump_log.py --list-ports
python3 tools/log_export/dump_log.py --port /dev/cu.usbmodemXXXX out.csv
python3 tools/log_export/dump_log.py --port /dev/cu.usbmodemXXXX --status
python3 tools/log_export/dump_log.py --port /dev/cu.usbmodemXXXX --clear
```

## VS Code tasks

`.vscode/tasks.json` wraps all four commands — **Terminal > Run Task…**:

| Task | What it does |
|---|---|
| `Logs: Status (is the device recording?)` | `--status`; the cheap thing to try first |
| `Logs: Download to CSV…` | prompts for an output path, auto-detects the port |
| `Logs: Download to CSV (choose port…)` | same, for when auto-detect picks the wrong device |
| `Logs: List serial ports` | `--list-ports` |
| `Logs: Erase device log storage (destructive)` | `--clear` — download first |

They run from the repo root (so a relative CSV path lands somewhere findable) using **`tools/monitor`'s virtualenv**, since `pyserial` is this script's only dependency and that venv already has it — run the `Monitor: Set up venv` task once if it doesn't exist yet. The download task's default output name is git-ignored; anything else you type is yours to manage.

**Stop recording on the device before downloading** (swipe up → tap the toggle off), or the last few seconds are still sitting in the RAM buffer unflushed.

## Notes

Only dependency is `pyserial` (already a `tools/monitor` dependency — `pip install pyserial` if running this standalone). Deliberately a plain CLI script rather than a `tools/monitor` tab: bulk binary dump/decode is a different job from `tools/monitor`'s live CAN/GUI work, not really a "mode" of the same app.

The device streams each chunk file back-to-back with no length prefix (see `core/logging/inc/log_record.hpp` for why); `dump_log.py` finds each chunk's boundary by scanning for the next valid `ChunkHeader`, then reorders records into true chronological order using each header's own `chunk_seq` (ever-increasing across the device's lifetime) rather than trusting stream order — rotation means an earlier byte position can hold a *newer* chunk than a later one, once the ring has wrapped.
