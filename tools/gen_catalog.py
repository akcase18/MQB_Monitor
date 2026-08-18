#!/usr/bin/env python3
"""Generates core/vehicle/src/signal_catalog.generated.inc from
core/vehicle/data/signals.json, the two-stage signal registry shared by the
firmware and the Mac monitor tool (tools/monitor). "Stage 1" is the whole
registry file -- every signal ever defined, active or WIP alike. "Stage 2" is
this script's output: only entries with `"status": "active"` (or no status
field at all, for older entries) are compiled in. WIP entries are skipped
entirely -- they exist in signals.json purely as the monitor tool's holding
area and never reach the firmware. Filtering preserves the registry file's
own order (never sorts) so that promoting/demoting a signal's status doesn't
reorder the ones around it -- see core/vehicle/README.md for why that matters
to the firmware's persisted per-signal config.

Run this after editing signals.json (by hand, or via the monitor tool's
Registry/Learn tabs) and before building the firmware. Not run automatically
by PlatformIO -- the generated file is checked in like any other source
file, so a build never silently picks up an un-reviewed catalog change.

Usage: tools/gen_catalog.py [--check]
  --check   Exit 1 if the generated file would change, without writing it
            (CI/pre-commit guard against an edited signals.json with a
            stale .inc committed alongside it).
"""
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SIGNALS_JSON = REPO_ROOT / "core/vehicle/data/signals.json"
OUT_PATH = REPO_ROOT / "core/vehicle/src/signal_catalog.generated.inc"
HPP_PATH = REPO_ROOT / "core/vehicle/inc/signal_catalog.hpp"

# The hand-declared count in the hpp that everything downstream sizes off of.
_COUNT_RE = re.compile(r"(SIGNAL_CATALOG_COUNT\s*=\s*)(\d+)(\s*;)")

MAX_REQUESTS_PER_SIGNAL = 2

HEADER = """\
/**
 * @file signal_catalog.generated.inc
 * @brief GENERATED FILE -- do not hand-edit. Produced by tools/gen_catalog.py
 * from core/vehicle/data/signals.json, filtered to that registry's "active"
 * entries only (WIP entries are excluded). Edit signals.json -- by hand, or
 * via the monitor tool's Registry/Learn tabs -- and re-run the script, then
 * commit both. Included by signal_catalog.cpp inside `namespace core`, after
 * the transform_* function definitions it references by name.
 */
"""


def fmt_float(v: float) -> str:
    return f"{v}f"


def fmt_request(req: dict) -> str:
    return (
        f"{{ {req['byte1']}, {req['byte2']}, "
        f"{fmt_float(req['offset'])}, {fmt_float(req['scale'])} }}"
    )


def active_signals(data: dict) -> list[dict]:
    """Stage 1 -> stage 2: the registry filtered to active entries, in
    registry-file order (never sorted -- see the module docstring)."""
    return [s for s in data["signals"] if s.get("status", "active") == "active"]


def build(data: dict) -> str:
    signals = active_signals(data)
    lines = [HEADER, "const SignalSpec SIGNAL_CATALOG[] = {"]

    for sig in signals:
        requests = sig["requests"]
        if len(requests) > MAX_REQUESTS_PER_SIGNAL:
            raise ValueError(
                f"signal id {sig['id']} ({sig['name']!r}) has "
                f"{len(requests)} requests, max is {MAX_REQUESTS_PER_SIGNAL}"
            )
        padded = [fmt_request(r) for r in requests]
        padded += ["{ 0, 0, 0.0f, 0.0f }"] * (
            MAX_REQUESTS_PER_SIGNAL - len(requests)
        )
        requests_str = ", ".join(padded)

        lines.append(
            f"    /* {sig['name']} */\n"
            f"    {{ {sig['id']},\n"
            f"      \"{sig['name']}\",\n"
            f"      UnitType::{sig['unit']},\n"
            f"      {sig['cycle_time_ms']},\n"
            f"      {{ {requests_str} }},\n"
            f"      {len(requests)},\n"
            f"      transform_{sig['transform']},\n"
            f"      {fmt_float(sig['display_min'])},\n"
            f"      {fmt_float(sig['display_max'])} }},"
        )

    lines.append("};")
    lines.append("")
    lines.append(
        f"inline constexpr size_t SIGNAL_CATALOG_JSON_COUNT = "
        f"{len(signals)};"
    )
    lines.append(
        "static_assert(SIGNAL_CATALOG_JSON_COUNT == SIGNAL_CATALOG_COUNT,\n"
        "    \"signal_catalog.hpp's SIGNAL_CATALOG_COUNT must match "
        "signals.json's entry count -- update the hpp\");"
    )
    lines.append("")
    return "\n".join(lines)


def _sync_hpp_count(count: int) -> str:
    """Returns signal_catalog.hpp's text with SIGNAL_CATALOG_COUNT set to
    `count`. Raises if the constant can't be found."""
    text = HPP_PATH.read_text()
    new_text, n = _COUNT_RE.subn(rf"\g<1>{count}\g<3>", text)
    if n != 1:
        raise RuntimeError(
            f"expected exactly one SIGNAL_CATALOG_COUNT in {HPP_PATH}, "
            f"found {n}"
        )
    return new_text


def main() -> int:
    check_only = "--check" in sys.argv[1:]

    data = json.loads(SIGNALS_JSON.read_text())

    # Duplicate-id check spans the WHOLE registry (active + WIP) -- an id
    # must be unique regardless of status, since WIP entries reserve it too.
    all_ids = [s["id"] for s in data["signals"]]
    if len(all_ids) != len(set(all_ids)):
        print(f"error: duplicate signal id in {SIGNALS_JSON}", file=sys.stderr)
        return 1

    active = active_signals(data)
    generated = build(data)
    hpp_synced = _sync_hpp_count(len(active))

    if check_only:
        current = OUT_PATH.read_text() if OUT_PATH.exists() else None
        if current != generated or HPP_PATH.read_text() != hpp_synced:
            print(
                f"error: generated catalog is stale relative to "
                f"{SIGNALS_JSON}\n  run: python3 tools/gen_catalog.py",
                file=sys.stderr,
            )
            return 1
        print("signal_catalog.generated.inc + SIGNAL_CATALOG_COUNT up to date")
        return 0

    OUT_PATH.write_text(generated)
    HPP_PATH.write_text(hpp_synced)
    print(
        f"wrote {OUT_PATH}: {len(active)} active of {len(all_ids)} total "
        f"registry signals, SIGNAL_CATALOG_COUNT synced"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
