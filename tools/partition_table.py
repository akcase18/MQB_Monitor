"""The ESP32 partition table (CSV), as the release tooling sees it.
Shared by tools/fw_version.py, which stamps layout_crc() into every dash
image as FW_PARTITION_LAYOUT, and tools/release.py, which compares it
between releases -- one implementation, so the two can't disagree.
"""

import zlib


def parse_size(text):
    """A partition-table number: decimal, 0x hex, or with a K/M suffix."""
    text = text.strip().lower()
    if not text:
        return None
    scale = 1
    if text[-1] in "km":
        scale = 1024 if text[-1] == "k" else 1024 * 1024
        text = text[:-1]
    return int(text, 0) * scale


def parse_rows(text):
    """A table's rows as (name, type, subtype, offset, size, flags),
    numbers parsed, everything lower-cased."""
    rows = []
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        cols = [c.strip().lower() for c in line.split(",")]
        cols += [""] * (6 - len(cols))
        name, ptype, subtype, offset, size, flags = cols[:6]
        rows.append((name, ptype, subtype, parse_size(offset), parse_size(size), flags))
    return rows


def partition_rows(path):
    with open(path) as f:
        return parse_rows(f.read())


def layout_crc(rows):
    """CRC-32 of the rows in a fixed form, so comments, spacing, case and
    how a number is written don't change it."""
    canon = "".join(
        "%s,%s,%s,%s,%s,%s\n"
        % (n, t, st, "" if o is None else "0x%x" % o, "" if sz is None else "0x%x" % sz, fl)
        for n, t, st, o, sz, fl in rows
    )
    return zlib.crc32(canon.encode()) & 0xFFFFFFFF


def fs_bytes(rows):
    for _, ptype, subtype, _, size, _ in rows:
        if ptype == "data" and subtype in ("spiffs", "littlefs"):
            return size
    return 0
