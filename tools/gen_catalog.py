#!/usr/bin/env python3
"""Generates core/vehicle/src/signal_catalog.generated.inc from
core/vehicle/data/signals.json, the two-stage signal registry shared by the
firmware and the Mac monitor tool (tools/monitor). "Stage 1" is the whole
registry file -- every signal ever defined, whatever its status. "Stage 2" is
this script's output, by status:

  * "active" (or no status field at all, for older entries): compiled in and
    offered to the user -- panels, the signal picker, the phone page.
  * "internal": compiled in for the firmware's own use (Dim Ctrl drives auto
    brightness) but never offered to the user. They sit after every active
    entry in SIGNAL_CATALOG[], past SIGNAL_CATALOG_COUNT, so everything that
    walks the user-facing catalog -- config records, the wire protocol, the
    pickers -- never sees them.
  * "wip": skipped entirely -- the monitor tool's holding area.

Filtering preserves the registry file's own order (never sorts) so that
promoting/demoting a signal's status doesn't reorder the ones around it --
see core/vehicle/README.md for why that matters to the firmware's persisted
per-signal config.

Run this after editing signals.json (by hand, or via the monitor tool's
Registry/Learn tabs) and before building the firmware. Not run automatically
by PlatformIO -- the generated file is checked in like any other source
file, so a build never silently picks up an un-reviewed catalog change.

It also writes the active signals' descriptions and Enum states into the
phone configurator page (platform/esp32/web/index.html), between its
GENERATED markers. Neither travels with the catalog over the wire --
RESP_STATE has no room for them -- so the page carries its own copy, keyed
by signal id.

Usage: tools/gen_catalog.py [--check]
  --check   Exit 1 if a generated file would change, without writing it
            (CI/pre-commit guard against an edited signals.json with a
            stale .inc committed alongside it).
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

# Importable however this file is loaded -- tools/monitor's checks load it
# by path, which doesn't put tools/ on the path.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_value_fonts import missing_glyphs  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
SIGNALS_JSON = REPO_ROOT / "core/vehicle/data/signals.json"
OUT_PATH = REPO_ROOT / "core/vehicle/src/signal_catalog.generated.inc"
HPP_PATH = REPO_ROOT / "core/vehicle/inc/signal_catalog.hpp"
WEB_PAGE = REPO_ROOT / "platform/esp32/web/index.html"

# The page's generated block: everything between these two lines.
_WEB_RE = re.compile(
    r"(/\* BEGIN GENERATED[^\n]*\n)(.*?)(^/\* END GENERATED \*/)",
    re.S | re.M,
)

# The counts in the hpp that everything downstream sizes off of.
_COUNT_RE = re.compile(r"(SIGNAL_CATALOG_COUNT\s*=\s*)(\d+)(\s*;)")
_INTERNAL_COUNT_RE = re.compile(r"(INTERNAL_SIGNAL_COUNT\s*=\s*)(\d+)(\s*;)")

# What each status means to the firmware -- see the module docstring.
STATUSES = ("active", "internal", "wip")

MAX_REQUESTS_PER_SIGNAL = 2

# core::UnitType, in order. The registry can't name a unit the firmware
# doesn't have.
FIRMWARE_UNITS = [
    "None", "Angle", "Percent", "Pressure", "RPM",
    "Speed", "Temp", "Torque", "Volume",
    "Length", "Voltage", "Current", "Illuminance", "Time", "TempDelta", "Enum",
]
# The module a registry with no active signals still polls nothing on; the
# ECU table can't be empty in C++.
DEFAULT_ECU = (0x7E0, 0x7E8)
ENUM_LABEL_MAX = 15  # core::ENUM_LABEL_MAX
# gui::PANEL_NAME_LEN and remote::protocol::WIRE_NAME_LEN, less the NUL: the
# dash and the phone both cut a longer name off.
NAME_MAX = 23
# The longest name that stays on one line in every panel: the smallest are
# 144 px wide, in 8 px label_sm glyphs. Past it the name wraps to a second
# line, and that panel's value and ring shrink to make room. A state is
# always Digital, whose words need less height than a ring, so it gets the
# full NAME_MAX.
NAME_ONE_LINE_MAX = 17
# FlexCAN_T4's setMBFilterRange() refuses a range wider than this
# (platform/teensy/src/teensy_can.cpp).
MAX_RX_SPAN = 1000
# core::SIGNAL_CONFIG_SLOTS: the saved config keeps settings for this many
# signals. Raising it is a config layout change (core/config/inc/config.hpp).
MAX_ACTIVE_SIGNALS = 64

HEADER = """\
/**
 * @file signal_catalog.generated.inc
 * @brief GENERATED FILE -- do not hand-edit. Produced by tools/gen_catalog.py
 * from core/vehicle/data/signals.json: that registry's "active" entries,
 * then its "internal" ones (WIP entries are excluded). Edit signals.json --
 * by hand, or via the monitor tool's Registry/Learn tabs -- and re-run the
 * script, then commit both. Included by signal_catalog.cpp inside `namespace core`, after
 * the transform_* function definitions it references by name.
 */
"""


def fmt_float(v: float) -> str:
    return f"{v}f"


def fmt_field(fld: dict | None) -> str:
    fld = fld or {}
    signed = "true" if fld.get("signed") else "false"
    little = "true" if fld.get("little_endian") else "false"
    return (
        f"{{ {int(fld.get('start', 0))}, {int(fld.get('len', 0))}, {signed}, "
        f"{int(fld.get('bit_shift', 0))}, {int(fld.get('bit_len', 0))}, {little} }}"
    )


def ecu_of(req: dict) -> tuple[int, int]:
    return _as_int(req["ecu"]["tx"]), _as_int(req["ecu"]["rx"])


def ecu_table(signals: list[dict]) -> list[tuple[int, int]]:
    """Every (tx, rx) module the active signals ask, in order of first use
    -- the firmware's UDS_ECUS[]; requests carry an index into it."""
    table: list[tuple[int, int]] = []
    for sig in signals:
        for req in sig["requests"]:
            ecu = ecu_of(req)
            if ecu not in table:
                table.append(ecu)
    return table or [DEFAULT_ECU]


def fmt_request(req: dict, ecus: list[tuple[int, int]]) -> str:
    return (
        f"{{ {req['byte1']}, {req['byte2']}, "
        f"{fmt_float(req['offset'])}, {fmt_float(req['scale'])}, "
        f"{fmt_field(req.get('field'))}, {ecus.index(ecu_of(req))} }}"
    )


def c_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _status(sig: dict) -> str:
    return sig.get("status", "active")


def active_signals(data: dict) -> list[dict]:
    """The user-facing part of stage 2: active entries, in registry-file
    order (never sorted -- see the module docstring)."""
    return [s for s in data["signals"] if _status(s) == "active"]


def internal_signals(data: dict) -> list[dict]:
    """Internal entries, in registry-file order."""
    return [s for s in data["signals"] if _status(s) == "internal"]


def firmware_signals(data: dict) -> list[dict]:
    """Everything compiled in, in SIGNAL_CATALOG[] order: the active
    entries, then the internal ones."""
    return active_signals(data) + internal_signals(data)


def _as_int(v) -> int:
    if isinstance(v, str):
        return int(v, 16) if v.lower().startswith("0x") else int(v)
    return int(v)


def validate(data: dict) -> list[str]:
    """Problems that stop the active and internal signals compiling, one
    line each."""
    problems = []
    for sig in data["signals"]:
        if _status(sig) not in STATUSES:
            problems.append(
                f"signal id {sig['id']} ({sig['name']!r}): unknown status "
                f"{_status(sig)!r}; use one of {', '.join(STATUSES)}"
            )
    for sig in firmware_signals(data):
        where = f"signal id {sig['id']} ({sig['name']!r})"
        unit = sig["unit"]
        name = sig["name"]
        name_max = NAME_MAX if unit == "Enum" else NAME_ONE_LINE_MAX
        if not name:
            problems.append(f"{where}: an {_status(sig)} signal needs a name")
        elif len(name) > name_max:
            problems.append(
                f"{where}: name is {len(name)} characters; keep it to "
                f"{name_max} or it "
                + ("gets cut off on the dash" if name_max == NAME_MAX
                   else "wraps into a Gauge on a small panel")
            )
        if not all(32 <= ord(c) < 127 for c in name):
            problems.append(
                f"{where}: name has characters the dash's font can't show "
                f"(plain ASCII only)"
            )
        if unit not in FIRMWARE_UNITS:
            problems.append(f"{where}: unknown unit {unit!r}")
        entries = sig.get("enum") or []
        if unit == "Enum" and not entries:
            problems.append(f"{where}: Enum signal has no enum entries")
        if len(entries) > 255:
            problems.append(f"{where}: more than 255 enum entries")
        values = [int(e["value"]) for e in entries]
        if len(values) != len(set(values)):
            problems.append(f"{where}: an enum value is listed twice")
        for e in entries:
            label = str(e["label"])
            if not label or len(label) > ENUM_LABEL_MAX:
                problems.append(
                    f"{where}: enum label {label!r} must be 1-"
                    f"{ENUM_LABEL_MAX} characters to fit a panel"
                )
            if not all(32 <= ord(c) < 127 for c in label):
                problems.append(
                    f"{where}: enum label {label!r} has characters the "
                    f"dash's font can't show (plain ASCII only)"
                )
            if not -2**31 <= int(e["value"]) < 2**31:
                problems.append(f"{where}: enum value {e['value']} out of range")
        for i, req in enumerate(sig["requests"]):
            ecu = req.get("ecu")
            if not ecu:
                problems.append(f"{where}: request {i} has no ECU set")
            elif not all(0 <= v <= 0x7FF for v in ecu_of(req)):
                problems.append(
                    f"{where}: request {i}'s ECU {ecu['tx']}/{ecu['rx']} "
                    f"isn't two 11-bit CAN ids"
                )
            fld = req.get("field")
            if fld:
                start, length = int(fld.get("start", 0)), int(fld.get("len", 0))
                shift, bits = int(fld.get("bit_shift", 0)), int(fld.get("bit_len", 0))
                if not 1 <= length <= 4 or not 0 <= start <= 255:
                    problems.append(
                        f"{where}: request {i}'s field must be 1-4 bytes"
                    )
                elif bits and (bits < 1 or shift + bits > length * 8):
                    problems.append(
                        f"{where}: request {i}'s bit range doesn't fit its "
                        f"{length}-byte field"
                    )
    # Panels and the signal pickers show the bare name, so two active
    # signals sharing one couldn't be told apart. Internal ones are held to
    # it too: promoting one to active must not collide.
    names = [s["name"] for s in firmware_signals(data) if s["name"]]
    for dup in sorted({n for n in names if names.count(n) > 1}):
        problems.append(f"two compiled signals are both named {dup!r}")
    count = len(active_signals(data))
    if count > MAX_ACTIVE_SIGNALS:
        problems.append(
            f"{count} active signals; the dash's saved config has room for "
            f"{MAX_ACTIVE_SIGNALS} (core::SIGNAL_CONFIG_SLOTS)"
        )
    # Modules are told apart by the id they answer on, and asked by the id
    # they listen on: neither may be shared by two different modules.
    if not problems:
        table = ecu_table(firmware_signals(data))
        rx_ids = [e[1] for e in table]
        if max(rx_ids) - min(rx_ids) > MAX_RX_SPAN:
            problems.append(
                f"the modules' response ids span 0x{min(rx_ids):03X}-"
                f"0x{max(rx_ids):03X}, wider than the Teensy's CAN filter "
                f"takes ({MAX_RX_SPAN} ids)"
            )
        for kind, pick in (("response", 1), ("request", 0)):
            ids = [e[pick] for e in table]
            for dup in sorted({i for i in ids if ids.count(i) > 1}):
                users = [f"{e[0]:03X}/{e[1]:03X}" for e in table if e[pick] == dup]
                problems.append(
                    f"modules {', '.join(users)} share {kind} id "
                    f"0x{dup:03X}; one module can't have two of the other"
                )
    return problems


def enum_table_name(sig: dict) -> str:
    return f"ENUM_{sig['id']}"


def build(data: dict) -> str:
    signals = firmware_signals(data)
    active_count = len(active_signals(data))
    ecus = ecu_table(signals)
    lines = [HEADER]
    lines.append("const UdsEcu UDS_ECUS[] = {")
    for tx, rx in ecus:
        lines.append(f"    {{ 0x{tx:03X}, 0x{rx:03X} }},")
    lines.append("};")
    lines.append(f"const uint8_t UDS_ECU_COUNT = {len(ecus)};")
    lines.append("")
    for sig in signals:
        entries = sig.get("enum") or []
        if sig["unit"] != "Enum" or not entries:
            continue
        rows = ", ".join(
            f"{{ {int(e['value'])}, {c_string(str(e['label']))} }}"
            for e in sorted(entries, key=lambda e: int(e["value"]))
        )
        lines.append(f"/* {sig['name']} */")
        lines.append(f"const EnumEntry {enum_table_name(sig)}[] = {{ {rows} }};")
        lines.append("")
    lines.append("const SignalSpec SIGNAL_CATALOG[] = {")

    for n, sig in enumerate(signals):
        if n == active_count:
            lines.append(
                "    /* Internal: the firmware's own, never offered to the "
                "user (see\n"
                "     * SIGNAL_CATALOG_COUNT). */"
            )
        requests = sig["requests"]
        if len(requests) > MAX_REQUESTS_PER_SIGNAL:
            raise ValueError(
                f"signal id {sig['id']} ({sig['name']!r}) has "
                f"{len(requests)} requests, max is {MAX_REQUESTS_PER_SIGNAL}"
            )
        padded = [fmt_request(r, ecus) for r in requests]
        padded += [f"{{ 0, 0, 0.0f, 0.0f, {fmt_field(None)}, 0 }}"] * (
            MAX_REQUESTS_PER_SIGNAL - len(requests)
        )
        requests_str = ", ".join(padded)
        entries = sig.get("enum") or []
        if sig["unit"] == "Enum" and entries:
            enum_str = f"{enum_table_name(sig)}, {len(entries)}"
        else:
            enum_str = "nullptr, 0"

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
            f"      {fmt_float(sig['display_max'])},\n"
            f"      {enum_str} }},"
        )

    lines.append("};")
    lines.append("")
    lines.append(
        f"inline constexpr size_t SIGNAL_CATALOG_JSON_COUNT = "
        f"{active_count};"
    )
    lines.append(
        f"inline constexpr size_t INTERNAL_SIGNAL_JSON_COUNT = "
        f"{len(signals) - active_count};"
    )
    lines.append(
        "static_assert(SIGNAL_CATALOG_JSON_COUNT == SIGNAL_CATALOG_COUNT\n"
        "                  && INTERNAL_SIGNAL_JSON_COUNT == "
        "INTERNAL_SIGNAL_COUNT,\n"
        "    \"signal_catalog.hpp's SIGNAL_CATALOG_COUNT/INTERNAL_SIGNAL_COUNT "
        "must match\"\n"
        "    \" signals.json's entry counts -- re-run tools/gen_catalog.py\");"
    )
    lines.append("")
    return "\n".join(lines)


def _js(value) -> str:
    """`value` as a JS literal. JSON is JS; "</" is escaped so a string
    can't end the <script> it sits in."""
    return json.dumps(value, ensure_ascii=False).replace("</", "<\\/")


def _sync_web_page(signals: list[dict]) -> str:
    """Returns index.html's text with its GENERATED block rebuilt from
    `signals`. Raises if the markers can't be found."""
    text = WEB_PAGE.read_text()
    descriptions = [
        f"  {sig['id']}: {_js(sig['description'])},\n"
        for sig in signals
        if sig.get("description")
    ]
    # Same order the firmware's ENUM_<id> tables use.
    enums = [
        f"  {sig['id']}: "
        + _js([
            [int(e["value"]), str(e["label"])]
            for e in sorted(sig["enum"], key=lambda e: int(e["value"]))
        ])
        + ",\n"
        for sig in signals
        if sig["unit"] == "Enum" and sig.get("enum")
    ]
    body = (
        "const SIGNAL_DESCRIPTIONS = {\n" + "".join(descriptions) + "};\n"
        "const SIGNAL_ENUMS = {\n" + "".join(enums) + "};\n"
    )
    new_text, n = _WEB_RE.subn(
        lambda m: m.group(1) + body + m.group(3), text
    )
    if n != 1:
        raise RuntimeError(
            f"expected exactly one GENERATED block in {WEB_PAGE}, found {n}"
        )
    return new_text


def _sync_hpp_count(count: int, internal: int) -> str:
    """Returns signal_catalog.hpp's text with SIGNAL_CATALOG_COUNT set to
    `count` and INTERNAL_SIGNAL_COUNT to `internal`. Raises if either
    constant can't be found."""
    text = HPP_PATH.read_text()
    for regex, value, name in (
        (_COUNT_RE, count, "SIGNAL_CATALOG_COUNT"),
        (_INTERNAL_COUNT_RE, internal, "INTERNAL_SIGNAL_COUNT"),
    ):
        text, n = regex.subn(rf"\g<1>{value}\g<3>", text)
        if n != 1:
            raise RuntimeError(
                f"expected exactly one {name} in {HPP_PATH}, found {n}"
            )
    return text


def main() -> int:
    check_only = "--check" in sys.argv[1:]

    data = json.loads(SIGNALS_JSON.read_text())

    # Duplicate-id check spans the WHOLE registry (active + WIP) -- an id
    # must be unique regardless of status, since WIP entries reserve it too.
    all_ids = [s["id"] for s in data["signals"]]
    if len(all_ids) != len(set(all_ids)):
        print(f"error: duplicate signal id in {SIGNALS_JSON}", file=sys.stderr)
        return 1

    problems = validate(data)
    if problems:
        print(f"error: {SIGNALS_JSON} has signals the firmware can't "
              f"compile:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1

    # Not an error: such a label still shows, just at the 24 px text size.
    small = missing_glyphs(data)
    if small:
        print("warning: these Enum labels have characters the panels' big "
              "value fonts lack, so they'll draw small:", file=sys.stderr)
        for label, lacking in small.items():
            print(f"  {label} (needs {lacking!r})", file=sys.stderr)
        print("  run tools/gen_value_fonts.py to add them", file=sys.stderr)

    active = active_signals(data)
    internal = internal_signals(data)
    generated = build(data)
    hpp_synced = _sync_hpp_count(len(active), len(internal))
    web_synced = _sync_web_page(active)

    if check_only:
        current = OUT_PATH.read_text() if OUT_PATH.exists() else None
        if (
            current != generated
            or HPP_PATH.read_text() != hpp_synced
            or WEB_PAGE.read_text() != web_synced
        ):
            print(
                f"error: generated catalog is stale relative to "
                f"{SIGNALS_JSON}\n  run: python3 tools/gen_catalog.py",
                file=sys.stderr,
            )
            return 1
        print(
            "signal_catalog.generated.inc, the hpp's counts and the "
            "page's signal tables up to date"
        )
        return 0

    OUT_PATH.write_text(generated)
    HPP_PATH.write_text(hpp_synced)
    WEB_PAGE.write_text(web_synced)
    print(
        f"wrote {OUT_PATH}: {len(active)} active and {len(internal)} "
        f"internal of {len(all_ids)} total registry signals, the hpp's "
        f"counts and the page's signal tables synced"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
