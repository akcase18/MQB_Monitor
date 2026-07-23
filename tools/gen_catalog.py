#!/usr/bin/env python3
"""Generates core/vehicle/src/signal_catalog.generated.inc from
core/vehicle/data/signals.json, the canonical signal catalog shared by the
firmware and the Mac monitor tool (tools/monitor).

Run this after editing signals.json (by hand, or via the monitor's "learn
mode" write-back) and before building the firmware. Not run automatically by
PlatformIO -- the generated file is checked in like any other source file,
so a build never silently picks up an un-reviewed catalog change.

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
 * from core/vehicle/data/signals.json, the canonical signal catalog. Edit
 * that file and re-run the script, then commit both. Included by
 * signal_catalog.cpp inside `namespace core`, after the transform_* function
 * definitions it references by name.
 */
"""


def fmt_float(v: float) -> str:
    return f"{v}f"


def fmt_request(req: dict) -> str:
    return (
        f"{{ {req['byte1']}, {req['byte2']}, "
        f"{fmt_float(req['offset'])}, {fmt_float(req['scale'])} }}"
    )


def build(data: dict) -> str:
    signals = data["signals"]
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
    ids = [s["id"] for s in data["signals"]]
    if len(ids) != len(set(ids)):
        print(f"error: duplicate signal id in {SIGNALS_JSON}", file=sys.stderr)
        return 1

    generated = build(data)
    hpp_synced = _sync_hpp_count(len(ids))

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
    print(f"wrote {OUT_PATH} and synced SIGNAL_CATALOG_COUNT ({len(ids)} signals)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
