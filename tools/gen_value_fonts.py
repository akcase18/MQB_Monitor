#!/usr/bin/env python3
"""Regenerates the panels' bold value fonts,
gui/assets/fonts/jetbrains_mono_bold_{30,38,54,64,80,92,128}.c, with digits plus
only the characters the active signals' Enum labels use. A full alphabet
would cost ~600 KB of flash, most of it in the biggest sizes; this
costs a few KB per letter.

A label with a character these fonts lack still shows, just smaller:
PanelWidget drops to the full-ASCII 24 px font for it. gen_catalog.py
warns when that's about to happen; run this to fix it.

Needs lv_font_conv (Node.js: `npx lv_font_conv`) and JetBrains Mono Bold
v2.002 -- the release the existing fonts were cut from, so new glyphs match
the old ones exactly:
https://github.com/JetBrains/JetBrainsMono/releases/download/v2.002/JetBrainsMono-2.002.zip
(ttf/JetBrainsMono-Bold.ttf). Later releases differ slightly, the minus
sign most of all.

Usage: tools/gen_value_fonts.py --font path/to/JetBrainsMono-Bold.ttf
         [--lv-font-conv "npx lv_font_conv"]
"""
from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SIGNALS_JSON = REPO_ROOT / "core/vehicle/data/signals.json"
FONT_DIR = REPO_ROOT / "gui/assets/fonts"
SIZES = (30, 38, 54, 64, 80, 92, 128)

# What every number needs, plus '?' for the "?<n>" a panel shows for an
# Enum value with no label.
BASE_SYMBOLS = "-.0123456789?"

_SYMBOLS_RE = re.compile(r"--symbols[= ](.*?) --format")


def font_path(size: int) -> Path:
    return FONT_DIR / f"jetbrains_mono_bold_{size}.c"


def needed_symbols(data: dict) -> str:
    """BASE_SYMBOLS, then every other character an active Enum label
    uses, sorted so the result only changes when the set does."""
    extra = set()
    for sig in data["signals"]:
        if sig.get("status", "active") != "active":
            continue
        for entry in sig.get("enum") or []:
            extra.update(entry["label"])
    return BASE_SYMBOLS + "".join(sorted(extra - set(BASE_SYMBOLS)))


def font_symbols(size: int = SIZES[0]) -> str | None:
    """The characters a generated value font holds, read back from the
    lv_font_conv options in its header. None if it isn't one of ours."""
    match = _SYMBOLS_RE.search(font_path(size).read_text())
    return match.group(1) if match else None


def missing_glyphs(data: dict) -> dict[str, str]:
    """Active Enum labels the value fonts can't draw, each with the
    characters it's missing."""
    have = set(font_symbols() or BASE_SYMBOLS)
    missing = {}
    for sig in data["signals"]:
        if sig.get("status", "active") != "active":
            continue
        for entry in sig.get("enum") or []:
            lacking = "".join(sorted(set(entry["label"]) - have))
            if lacking:
                missing[f'{sig["name"]}: "{entry["label"]}"'] = lacking
    return missing


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--font", required=True, type=Path,
                        help="JetBrainsMono-Bold.ttf, v2.002")
    parser.add_argument("--lv-font-conv", default="npx lv_font_conv",
                        help='command to run (default: "npx lv_font_conv")')
    args = parser.parse_args()

    ttf = args.font.resolve()
    symbols = needed_symbols(json.loads(SIGNALS_JSON.read_text()))
    for size in SIZES:
        out = font_path(size)
        cmd = shlex.split(args.lv_font_conv) + [
            "--font", str(ttf), "--bpp", "4", "--size", str(size),
            # One argument with '=': the set starts with '-', which
            # lv_font_conv would otherwise take for an option.
            "--no-compress", f"--symbols={symbols}", "--format", "lvgl",
            "-o", str(out.relative_to(REPO_ROOT)),
        ]
        subprocess.run(cmd, cwd=REPO_ROOT, check=True)
        # The header records the options; keep the local path out of it.
        out.write_text(out.read_text().replace(str(ttf), ttf.name, 1))
        print(f"wrote {out.relative_to(REPO_ROOT)}")
    print(f"value fonts now hold: {symbols}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
