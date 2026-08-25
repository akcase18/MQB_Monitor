"""Decoding of the firmware's on-disk log formats -- chunk files and the
capture index. Pure data: no serial, no Qt, so the CLI (dump_log.py), the
GUI and the tests all decode through this one implementation.

Layout is defined by core/logging/inc/log_record.hpp, which is #pragma
pack'ed precisely so this hand-ported struct set can read it back:

  ChunkHeader { u32 magic, u8 version, u32 session_id, u32 chunk_seq }  13B
  LogRecord   { u32 timestamp_ms, u16 signal_id, f32 value }            10B

A chunk carries NO record count -- the firmware can't patch one in after
the fact (IStorage has no seek, and records are appended across many
flushes over a chunk's life). So a reader derives each chunk's extent the
same way the firmware's dump does: scan forward to the next byte sequence
that parses as a valid header, which also terminates naturally at EOF for
the last chunk in the stream.

Stream order is NOT chronological order. The ring rotates through whole
files, so once it has lapped, an earlier stream position can hold a *newer*
chunk than a later one. Every ordering here comes from the header's own
chunk_seq (ever-increasing across the device's lifetime), never from
position in the stream.
"""
from __future__ import annotations

import csv
import json
import re
import struct
from dataclasses import dataclass, field
from pathlib import Path

CHUNK_HEADER_FMT = "<IBII"  # magic, version, session_id, chunk_seq
CHUNK_HEADER_LEN = struct.calcsize(CHUNK_HEADER_FMT)
LOG_RECORD_FMT = "<IHf"  # timestamp_ms, signal_id, value
LOG_RECORD_LEN = struct.calcsize(LOG_RECORD_FMT)

LOG_MAGIC = 0x4C4F4731  # ASCII "LOG1" -- keep in sync with log_record.hpp
LOG_VERSION = 1

INDEX_HEADER_FMT = "<IBIH"  # magic, version, next_log_id, entry_count
INDEX_HEADER_LEN = struct.calcsize(INDEX_HEADER_FMT)
INDEX_ENTRY_FMT = "<IIIII"  # log_id, session_id, first/last_chunk_seq, duration_ms
INDEX_ENTRY_LEN = struct.calcsize(INDEX_ENTRY_FMT)

LOG_INDEX_MAGIC = 0x4C495831  # ASCII "LIX1"
LOG_INDEX_VERSION = 1
LOG_INDEX_NAME = "log_index.bin"

# Chunk files are `/log_%03u.bin` (core/logging's chunk_path()); the index
# file deliberately doesn't match that shape so a scan can't mistake it for
# a chunk. Same rule here when reading a simulator storage directory.
CHUNK_FILE_RE = re.compile(r"^log_\d{3}\.bin$")

# Repo layout: tools/log_export/mqb_logs/logfile.py -> repo root is 3 up.
REPO_ROOT = Path(__file__).resolve().parents[3]
SIGNALS_JSON = REPO_ROOT / "core/vehicle/data/signals.json"

CSV_COLUMNS = [
    "session_id", "chunk_seq", "timestamp_ms", "signal_id", "signal_name", "value",
]

Record = tuple[int, int, float]  # timestamp_ms, signal_id, value


@dataclass
class Chunk:
    """One chunk file's worth of decoded records, as it appeared in the
    stream. `chunk_seq` -- not stream position -- is what orders it."""

    session_id: int
    chunk_seq: int
    records: list[Record]


@dataclass
class Session:
    """Every chunk sharing one session_id: one power cycle of the device.

    This is the coarsest grouping the dump itself can prove. A *capture*
    (one Logging on -> off cycle, what the user calls "a log") is finer, and
    LOGDUMP can't resolve it: the capture index isn't part of the stream,
    and even with it in hand several captures can share one chunk with
    nothing in the records marking where one ended and the next began. See
    read_index() and the README.
    """

    session_id: int
    chunks: list[Chunk] = field(default_factory=list)

    @property
    def record_count(self) -> int:
        return sum(len(c.records) for c in self.chunks)

    @property
    def chunk_seq_range(self) -> tuple[int, int]:
        seqs = [c.chunk_seq for c in self.chunks]
        return (min(seqs), max(seqs)) if seqs else (0, 0)

    @property
    def signal_ids(self) -> set[int]:
        return {r[1] for c in self.chunks for r in c.records}

    @property
    def timestamp_span_ms(self) -> tuple[int, int]:
        """(first, last) boot-relative timestamps, or (0, 0) if empty.
        Boot-relative is all there is -- no RTC exists on this hardware --
        which is exactly why it's only meaningful within one session."""
        stamps = [r[0] for c in self.chunks for r in c.records]
        return (min(stamps), max(stamps)) if stamps else (0, 0)

    @property
    def duration_ms(self) -> int:
        first, last = self.timestamp_span_ms
        return last - first


@dataclass
class IndexEntry:
    """One capture, from the device's `/log_index.bin` side file. Only
    readable from a storage directory (the simulator's, or a card pulled
    from the device) -- LOGDUMP streams chunks only."""

    log_id: int
    session_id: int
    first_chunk_seq: int
    last_chunk_seq: int
    duration_ms: int


def find_next_header(data: bytes, start: int) -> int:
    """@returns the offset of the next byte sequence at/after `start` that
    parses as a valid ChunkHeader (magic + version match), or len(data) if
    none exists."""
    pos = start
    while True:
        pos = data.find(struct.pack("<I", LOG_MAGIC), pos)
        if pos == -1 or pos + CHUNK_HEADER_LEN > len(data):
            return len(data)
        _magic, version, _session_id, _chunk_seq = struct.unpack_from(
            CHUNK_HEADER_FMT, data, pos
        )
        if version == LOG_VERSION:
            return pos
        pos += 1  # false match (magic bytes appeared inside record data)


def parse_chunks(data: bytes) -> list[Chunk]:
    """Decodes a raw LOGDUMP stream (or a concatenated storage directory)
    into chunks, in the order they appeared. NOT sorted -- callers that care
    about chronology use sort_chunks()."""
    chunks: list[Chunk] = []
    pos = 0
    while pos < len(data):
        header_end = find_next_header(data, pos)
        if header_end != pos:
            # Garbage before the next real header (shouldn't happen with a
            # well-formed dump) -- skip it rather than misparsing it.
            pos = header_end
            continue
        if pos + CHUNK_HEADER_LEN > len(data):
            break

        _magic, _version, session_id, chunk_seq = struct.unpack_from(
            CHUNK_HEADER_FMT, data, pos
        )
        body_start = pos + CHUNK_HEADER_LEN
        body_end = find_next_header(data, body_start)
        body = data[body_start:body_end]

        record_count = len(body) // LOG_RECORD_LEN  # drops a torn trailing
        # partial record, if the dump was interrupted mid-write
        chunks.append(Chunk(
            session_id=session_id,
            chunk_seq=chunk_seq,
            records=[
                struct.unpack_from(LOG_RECORD_FMT, body, i * LOG_RECORD_LEN)
                for i in range(record_count)
            ],
        ))
        pos = body_end
    return chunks


def sort_chunks(chunks: list[Chunk]) -> list[Chunk]:
    """True chronological order: by chunk_seq, which never resets and never
    wraps (unlike the file-name slot it lands in). Records within a chunk
    are already in the order they were sampled."""
    return sorted(chunks, key=lambda c: c.chunk_seq)


def group_sessions(chunks: list[Chunk]) -> list[Session]:
    """Chunks grouped by session_id, newest session first (highest
    session_id -- it increments once per boot), each session's chunks in
    chunk_seq order."""
    by_session: dict[int, Session] = {}
    for chunk in sort_chunks(chunks):
        by_session.setdefault(chunk.session_id, Session(chunk.session_id)).chunks.append(chunk)
    return sorted(by_session.values(), key=lambda s: s.session_id, reverse=True)


def parse_index(data: bytes) -> list[IndexEntry]:
    """Decodes `/log_index.bin`. Returns [] for anything that doesn't check
    out -- the index is a cache, not the source of truth, so a corrupt one
    degrades to "no capture info", never to wrong capture info."""
    if len(data) < INDEX_HEADER_LEN:
        return []
    magic, version, _next_log_id, entry_count = struct.unpack_from(INDEX_HEADER_FMT, data, 0)
    if magic != LOG_INDEX_MAGIC or version != LOG_INDEX_VERSION:
        return []

    entries: list[IndexEntry] = []
    for i in range(entry_count):
        offset = INDEX_HEADER_LEN + i * INDEX_ENTRY_LEN
        if offset + INDEX_ENTRY_LEN > len(data):
            break  # truncated: an incomplete index, not a wrong one
        entries.append(IndexEntry(*struct.unpack_from(INDEX_ENTRY_FMT, data, offset)))
    return entries


def read_storage_dir(directory: Path) -> bytes:
    """Concatenates a storage directory's chunk files into the same byte
    stream LOGDUMP would produce: array-index order, skipping the index file
    and anything else that isn't a chunk."""
    paths = sorted(p for p in directory.iterdir() if CHUNK_FILE_RE.match(p.name))
    return b"".join(p.read_bytes() for p in paths)


def read_index(directory: Path) -> list[IndexEntry]:
    """The capture index from a storage directory, or [] if absent."""
    path = directory / LOG_INDEX_NAME
    try:
        return parse_index(path.read_bytes())
    except OSError:
        return []


def load_signal_names(signals_json: Path = SIGNALS_JSON) -> dict[int, str]:
    """id -> name, for a friendlier CSV. Best-effort -- an empty dict just
    means output falls back to raw ids."""
    try:
        data = json.loads(signals_json.read_text())
        return {s["id"]: s["name"] for s in data["signals"]}
    except (OSError, KeyError, json.JSONDecodeError):
        return {}


def csv_rows(chunks: list[Chunk], names: dict[int, str]):
    """Yields one CSV row per record, in true chronological order."""
    for chunk in sort_chunks(chunks):
        for timestamp_ms, signal_id, value in chunk.records:
            yield [
                chunk.session_id,
                chunk.chunk_seq,
                timestamp_ms,
                signal_id,
                names.get(signal_id, ""),
                value,
            ]


def write_csv(chunks: list[Chunk], out_path: Path, names: dict[int, str]) -> int:
    """@returns how many records were written."""
    total = 0
    with out_path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(CSV_COLUMNS)
        for row in csv_rows(chunks, names):
            writer.writerow(row)
            total += 1
    return total


def format_duration(ms: int) -> str:
    """m:ss for anything under an hour, h:mm:ss past that."""
    seconds, ms_part = divmod(max(0, ms), 1000)
    minutes, seconds = divmod(seconds, 60)
    hours, minutes = divmod(minutes, 60)
    if hours:
        return f"{hours}:{minutes:02d}:{seconds:02d}"
    return f"{minutes}:{seconds:02d}.{ms_part // 100}"


def format_bytes(n: int) -> str:
    if n < 1024:
        return f"{n} B"
    if n < 1024 * 1024:
        return f"{n / 1024:.1f} KB"
    return f"{n / (1024 * 1024):.2f} MB"
