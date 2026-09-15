#!/usr/bin/env python3
"""Generates each product's compiled signal catalog from its registry.

This project builds two products from one tree, and **they share no signals
at all** -- an MQB car cannot report oil pressure, and the analog gauge has
no vehicle bus to ask. So each has its own registry under
core/vehicle/data/<product>/signals.json, and its own generated output under
core/vehicle/generated/<product>/. platformio.ini puts exactly one of those
directories on the include path per environment, which is the same
"configuration picks the file" mechanism build_src_filter already uses to
pick a board -- no #ifdef anywhere in core/.

Per product, one filtered pass over the registry emits:

  <gen>/signal_catalog.generated.inc   SIGNAL_CATALOG[]  (presentation)
  <gen>/signal_catalog_count.inc       SIGNAL_CATALOG_COUNT
  <gen>/signal_defaults.generated.inc  the default panel assignment

and, for a product acquired over a bus:

  <bind>/uds_binding.generated.inc     UDS_BINDINGS[]    (acquisition)
  <bind>/uds_binding_count.inc         UDS_BINDING_COUNT

The catalog and binding tables are PARALLEL ARRAYS -- entry i of each
describes the same signal -- which holds by construction because both come
out of the same filtered pass. Nothing else maintains that correspondence,
which is why they are emitted together rather than by two scripts that
could drift.

The counts are emitted as their own tiny include rather than regex-patched
into signal_catalog.hpp, which is what an earlier version did. A hand-
declared count in a shared header cannot describe two products at once, and
a header that a script rewrites in place is a trap for anyone editing it.

signals.json is a two-stage registry. "Stage 1" is the whole file -- every
signal ever defined, active or WIP. "Stage 2" is this script's output: only
entries with `"status": "active"` (or no status field, for older entries)
are compiled in. WIP entries exist purely as the monitor tool's holding
area and never reach the firmware. Filtering preserves the registry's own
order (never sorts) so promoting or demoting a signal does not reorder the
ones around it -- see core/vehicle/README.md for why that matters to the
persisted per-signal config.

Run after editing any signals.json and before building. Not run
automatically by PlatformIO: the generated files are checked in like any
other source, so a build never silently picks up an un-reviewed catalog.

Usage: tools/gen_catalog.py [--check] [--product NAME]
  --check          Exit 1 if any generated file would change, without
                   writing (CI / pre-commit guard against an edited
                   registry with a stale .inc committed alongside it).
  --product NAME   Only this product. Default: all of them.
"""
import json
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

MAX_REQUESTS_PER_SIGNAL = 2
MAX_PANELS = 6  # mirrors core::MAX_PANELS; priority_ids is padded to this


@dataclass(frozen=True)
class Product:
    """One buildable product's registry and where its output goes."""

    name: str
    registry: Path
    gen_dir: Path
    # None for a product with no vehicle bus -- the gauge acquires its
    # signals from ADC channels described by runtime core::SensorCal, so
    # there is nothing to generate.
    bind_dir: Path | None


PRODUCTS = [
    Product(
        name="mqb",
        registry=REPO_ROOT / "core/vehicle/data/mqb/signals.json",
        gen_dir=REPO_ROOT / "core/vehicle/generated/mqb",
        bind_dir=REPO_ROOT / "core/can/generated/mqb",
    ),
    Product(
        name="gauge",
        registry=REPO_ROOT / "core/vehicle/data/gauge/signals.json",
        gen_dir=REPO_ROOT / "core/vehicle/generated/gauge",
        bind_dir=None,
    ),
]


def banner(filename: str, product: str, body: str) -> str:
    return (
        f"/**\n"
        f" * @file {filename}\n"
        f" * @brief GENERATED FILE -- do not hand-edit. Produced by\n"
        f" * tools/gen_catalog.py from core/vehicle/data/{product}/signals.json,\n"
        f" * filtered to that registry's \"active\" entries only (WIP entries are\n"
        f" * excluded). Edit the registry -- by hand, or via the monitor tool's\n"
        f" * Registry/Learn tabs -- re-run the script, and commit both.\n"
        f" *\n"
        f"{body}"
        f" */\n"
    )


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


def build_catalog(product: Product, signals: list[dict]) -> str:
    lines = [
        banner(
            "signal_catalog.generated.inc",
            product.name,
            " * Included by signal_catalog.cpp inside `namespace core`.\n"
            " *\n"
            " * Presentation metadata only -- name, unit, fallback display\n"
            " * range. How a signal is *acquired* is deliberately not here;\n"
            " * see the product's own acquisition layer.\n",
        ),
        "const SignalSpec SIGNAL_CATALOG[] = {",
    ]

    for sig in signals:
        lines.append(
            f"    /* {sig['name']} */\n"
            f"    {{ {sig['id']},\n"
            f"      \"{sig['name']}\",\n"
            f"      UnitType::{sig['unit']},\n"
            f"      {fmt_float(sig['display_min'])},\n"
            f"      {fmt_float(sig['display_max'])} }},"
        )

    lines += [
        "};",
        "",
        "static_assert(sizeof(SIGNAL_CATALOG) / sizeof(SIGNAL_CATALOG[0])",
        "                  == SIGNAL_CATALOG_COUNT,",
        "              \"SIGNAL_CATALOG_COUNT disagrees with the array it \"",
        "              \"sizes -- regenerate both with tools/gen_catalog.py\");",
        "",
    ]
    return "\n".join(lines)


def build_count(product: Product, name: str, filename: str, n: int) -> str:
    return (
        banner(
            filename,
            product.name,
            " * Its own file, rather than a constant in the header that uses\n"
            " * it, because two products build from this tree and a single\n"
            " * hand-declared count cannot describe both. platformio.ini puts\n"
            " * one product's generated directory on the include path.\n",
        )
        + f"\ninline constexpr size_t {name} = {n};\n"
    )


def build_defaults(product: Product, data: dict, signals: list[dict]) -> str:
    """The factory-default panel assignment.

    Generated rather than hardcoded in ConfigStore::set_defaults(), which
    used to carry MQB catalog ids in a comment-annotated block -- correct
    for exactly one product, and silently wrong for the other, since an id
    that is not in the compiled catalog resolves to -1 and shows nothing.
    """
    defaults = data.get("defaults", {})
    ids = list(defaults.get("priority_ids", [s["id"] for s in signals]))
    if not ids:
        raise ValueError(f"{product.registry}: no default priority ids")

    known = {s["id"] for s in signals}
    for sig_id in ids:
        if sig_id not in known:
            raise ValueError(
                f"{product.registry}: defaults.priority_ids names id "
                f"{sig_id}, which is not an active signal"
            )

    # Every rank up to MAX_PANELS needs a real, resolvable id even when the
    # product shows fewer panels than that -- Config::priorities is a fixed
    # array and the ranks past num_panels are simply inactive, not absent.
    padded = [ids[i % len(ids)] for i in range(MAX_PANELS)]

    num_panels = int(defaults.get("num_panels", min(len(ids), MAX_PANELS)))
    unit_system = defaults.get("unit_system", "Imperial")

    by_id = {s["id"]: s["name"] for s in signals}
    rows = ",\n".join(
        f"    {{ {sig_id}, UnitSystem::{unit_system} }}"
        f" /* {by_id[sig_id]} */"
        for sig_id in padded
    )

    return (
        banner(
            "signal_defaults.generated.inc",
            product.name,
            " * Included by config_store.cpp inside `namespace core`.\n"
            " *\n"
            " * The factory panel assignment, by priority rank (0 = most\n"
            " * important), padded to MAX_PANELS. Ranks past\n"
            " * DEFAULT_NUM_PANELS are real but inactive.\n",
        )
        + f"\ninline constexpr uint8_t DEFAULT_NUM_PANELS = {num_panels};\n"
        + "\ninline constexpr PrioritizedSignal DEFAULT_PRIORITIES[] = {\n"
        + rows
        + ",\n};\n"
        + "\nstatic_assert(sizeof(DEFAULT_PRIORITIES)"
        + " / sizeof(DEFAULT_PRIORITIES[0])\n"
        + "                  == MAX_PANELS,\n"
        + "              \"DEFAULT_PRIORITIES must cover every panel rank\");\n"
        + "static_assert(DEFAULT_NUM_PANELS >= 1"
        + " && DEFAULT_NUM_PANELS <= MAX_PANELS,\n"
        + "              \"DEFAULT_NUM_PANELS out of range\");\n"
    )


def build_bindings(product: Product, signals: list[dict]) -> str:
    lines = [
        banner(
            "uds_binding.generated.inc",
            product.name,
            " * Included by uds_binding.cpp inside `namespace core`, after the\n"
            " * transform_* definitions it references by name.\n"
            " *\n"
            " * PARALLEL ARRAY to this product's signal_catalog.generated.inc:\n"
            " * entry i here describes entry i there. Both come from one\n"
            " * filtered pass, so the correspondence holds by construction.\n",
        ),
        "const UdsSignalBinding UDS_BINDINGS[] = {",
    ]

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

        lines.append(
            f"    /* {sig['name']} */\n"
            f"    {{ {sig['id']},\n"
            f"      {sig['cycle_time_ms']},\n"
            f"      {{ {', '.join(padded)} }},\n"
            f"      {len(requests)},\n"
            f"      transform_{sig['transform']} }},"
        )

    lines += [
        "};",
        "",
        "static_assert(sizeof(UDS_BINDINGS) / sizeof(UDS_BINDINGS[0])",
        "                  == UDS_BINDING_COUNT,",
        "              \"UDS_BINDING_COUNT disagrees with the array it \"",
        "              \"sizes -- regenerate both with tools/gen_catalog.py\");",
        "",
    ]
    return "\n".join(lines)


def outputs_for(product: Product) -> list[tuple[Path, str]]:
    data = json.loads(product.registry.read_text())

    # Duplicate-id check spans the WHOLE registry (active + WIP) -- an id
    # must be unique regardless of status, since a WIP entry reserves it.
    all_ids = [s["id"] for s in data["signals"]]
    if len(all_ids) != len(set(all_ids)):
        raise ValueError(f"{product.registry}: duplicate signal id")

    signals = active_signals(data)
    if not signals:
        raise ValueError(f"{product.registry}: no active signals")

    out = [
        (
            product.gen_dir / "signal_catalog.generated.inc",
            build_catalog(product, signals),
        ),
        (
            product.gen_dir / "signal_catalog_count.inc",
            build_count(
                product,
                "SIGNAL_CATALOG_COUNT",
                "signal_catalog_count.inc",
                len(signals),
            ),
        ),
        (
            product.gen_dir / "signal_defaults.generated.inc",
            build_defaults(product, data, signals),
        ),
    ]

    if product.bind_dir is not None:
        out += [
            (
                product.bind_dir / "uds_binding.generated.inc",
                build_bindings(product, signals),
            ),
            (
                product.bind_dir / "uds_binding_count.inc",
                build_count(
                    product,
                    "UDS_BINDING_COUNT",
                    "uds_binding_count.inc",
                    len(signals),
                ),
            ),
        ]

    return out


def main() -> int:
    args = sys.argv[1:]
    check_only = "--check" in args

    products = PRODUCTS
    if "--product" in args:
        want = args[args.index("--product") + 1]
        products = [p for p in PRODUCTS if p.name == want]
        if not products:
            print(f"error: unknown product {want!r}", file=sys.stderr)
            return 1

    stale: list[Path] = []
    written: list[Path] = []

    for product in products:
        try:
            outputs = outputs_for(product)
        except (ValueError, KeyError) as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

        for path, want in outputs:
            have = path.read_text() if path.exists() else None
            if check_only:
                if have != want:
                    stale.append(path)
                continue
            if have != want:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(want)
                written.append(path)

    if check_only:
        if stale:
            names = "\n  ".join(str(p.relative_to(REPO_ROOT)) for p in stale)
            print(
                f"error: generated catalog is stale:\n  {names}\n"
                f"  run: python3 tools/gen_catalog.py",
                file=sys.stderr,
            )
            return 1
        names = ", ".join(p.name for p in products)
        print(f"generated catalogs up to date ({names})")
        return 0

    if not written:
        print("no changes")
        return 0
    for path in written:
        print(f"wrote {path.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
