# tools/log_export/

Pulls the dashboard firmware's black-box signal log (see [core/logging](../../core/logging/README.md)) over USB serial and decodes it to CSV — as a **GUI app** or a **CLI script**, both over the same code.

- **[`run.py`](run.py)** — the **MQB Log Export** app: a PySide6 GUI built on the same shell as [`tools/monitor`](../monitor/README.md) (shared top bar / sidebar / theme), for when you want to see what's on the device before deciding what to keep.
- **[`dump_log.py`](dump_log.py)** — the scriptable CLI, unchanged in interface. What the VS Code tasks below run.
- **[`mqb_logs/`](mqb_logs/)** — what both go through, so they can't drift apart:
  - `logfile.py` — the on-disk chunk/index decode. Pure data: no serial, no Qt.
  - `device.py` — the `LOGSTATUS`/`LOGDUMP`/`LOGCLEAR` command set, plus a background dump worker. **Not** the SLCAN protocol `env:can_sniffer`/`tools/monitor` speak — a completely separate board/firmware/protocol for live CAN sniffing, unrelated to this.
  - `gui.py` — the app. `theme.py` re-exports the monitor's palette rather than keeping a second copy.

## The app

```sh
tools/monitor/.venv/bin/python tools/log_export/run.py                    # auto-detect the port
tools/monitor/.venv/bin/python tools/log_export/run.py --port /dev/cu...  # a specific port
tools/monitor/.venv/bin/python tools/log_export/run.py --folder sim_storage
tools/monitor/.venv/bin/python tools/log_export/run.py --check sim_storage  # headless decode
```

Two pages:

- **Device** — what `LOGSTATUS` says (ready, session, captures, recorded time, current chunk), an amber banner while the device is *still recording*, and the three actions: **Download Log**, **Save Raw Dump…** (the undecoded bytes, worth keeping if a decode ever looks wrong), **Erase Device Log…** (`LOGCLEAR`, behind a confirmation). A dump shows a running byte count and can be cancelled — chunk boundaries are self-describing, so a cut-short dump is a *partial* log, not a corrupt one.
- **Logs** — what came back, grouped into sessions, with a record preview and **Export CSV…** (selected sessions, or all of them). Same columns `dump_log.py` writes.

**Source** in the top bar is either a serial port or a **storage folder** — the simulator's `sim_storage/`, or a copy of one. The folder source is this app's equivalent of the monitor's Demo mode: the whole decode/preview/export path works with no hardware plugged in. `--check` is the headless version of it (no PySide6 needed), and is the quickest way to see whether a dump decoded.

### Sessions, not captures

The app groups records by **session** — one power cycle, `session_id` in every chunk header — not by **capture** (one Logging on → off cycle, which is what the [LoggingScreen](../../gui/screens/README.md) means by "a log"). A dump can't resolve captures: `/log_index.bin` isn't part of the `LOGDUMP` stream, and even read straight off a storage directory it only bounds a capture to a *chunk range* — several captures routinely share one chunk, with nothing in the records marking where one ended. So the capture **count** is reported (it's a true fact about the device) and grouping stops at the session. Splitting a session into its captures would need the firmware to stream the index, or to write a capture marker into the chunks themselves — tracked in [TODO.md](../../TODO.md).

Sessions that recorded nothing (a boot with logging left off) are hidden by default, behind a **Show N empty sessions** checkbox next to the summary — they'd otherwise interleave with the ones you're picking between. Flipping it keeps whatever you had selected.

## The CLI

```sh
python3 tools/log_export/dump_log.py --list-ports
python3 tools/log_export/dump_log.py --port /dev/cu.usbmodemXXXX out.csv
python3 tools/log_export/dump_log.py --port /dev/cu.usbmodemXXXX --status
python3 tools/log_export/dump_log.py --port /dev/cu.usbmodemXXXX --clear
```

## VS Code tasks

`.vscode/tasks.json` wraps the app and all four CLI commands — **Terminal > Run Task…**:

| Task | What it does |
|---|---|
| `Logs: Open Log Export app` | the GUI, auto-detecting the port |
| `Logs: Open Log Export app (simulator storage)` | the GUI on `sim_storage/`, no hardware |
| `Logs: Status (is the device recording?)` | `--status`; the cheap thing to try first |
| `Logs: Download to CSV…` | prompts for an output path, auto-detects the port |
| `Logs: Download to CSV (choose port…)` | same, for when auto-detect picks the wrong device |
| `Logs: List serial ports` | `--list-ports` |
| `Logs: Erase device log storage (destructive)` | `--clear` — download first |

They run from the repo root (so a relative CSV path lands somewhere findable) using **`tools/monitor`'s virtualenv**, which already has both dependencies — run the `Monitor: Set up venv` task once if it doesn't exist yet. The download task's default output name is git-ignored; anything else you type is yours to manage.

**Stop recording on the device before downloading** (swipe up → tap the toggle off), or the last few seconds are still sitting in the RAM buffer unflushed. The app's Device page shows an amber banner when this applies.

## Notes

`dump_log.py` and `--check` need only **pyserial**; the GUI additionally needs **PySide6**. Both are already `tools/monitor` dependencies, which is why this tool borrows that venv rather than keeping its own.

The device streams each chunk file back-to-back with no length prefix (see `core/logging/inc/log_record.hpp` for why); `logfile.py` finds each chunk's boundary by scanning for the next valid `ChunkHeader`, then reorders records into true chronological order using each header's own `chunk_seq` (ever-increasing across the device's lifetime) rather than trusting stream order — rotation means an earlier byte position can hold a *newer* chunk than a later one, once the ring has wrapped. `LOGDUMP` also has no end-of-stream marker, so the read ends on two seconds of silence; that's the same reason a download reports bytes received rather than a percentage.
