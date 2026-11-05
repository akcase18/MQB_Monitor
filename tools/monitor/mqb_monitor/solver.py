"""Works out a signal's decode from its stored raw -> true-value points.

Two kinds of answer, matching the two kinds of signal:

  * fit_points / search_fields -- a NUMERIC signal: least-squares offset and
    scale for `value = (raw + offset) * scale`, the only form the firmware
    decodes. search_fields tries every byte-aligned field in the payload, for
    finding where a value sits inside a multi-value DID.
  * fit_enum / search_enum_fields -- a LABELLED signal (gear, valve state):
    which raw value means which label. There is no offset or scale; the
    search also tries single bits and nibbles, since a state is often a flag.

Nothing here changes a signal. Each result says plainly whether the points
support it: a lot of signals aren't a straight-line scale of one field at
all (lookup tables, packed flags, values the phone computes from several
DIDs), and "doesn't fit" is a normal answer, not an error.

Points are in the signal's stored (metric) unit. No PySide6.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field

from .catalog import EnumEntry, Field, Point, decode_field

# A line through two points is always exact, so fewer than this many points
# can't confirm a fit.
CONFIRM_POINTS = 3


def _decimals(v: float) -> int:
    text = f"{v:.6f}".rstrip("0")
    return len(text) - text.index(".") - 1


def display_tolerance(values: list[float]) -> float:
    """How far a true value may sit from the fitted line and still match:
    half a step of the precision the values were written down at (32.3 ->
    +/-0.05). The phone rounds what it shows, so an exact decode can still
    miss the written value by that much."""
    d = max((_decimals(v) for v in values), default=0)
    return 0.5 * 10.0 ** -d + 1e-9


def _sig_round(v: float, digits: int) -> float:
    if v == 0:
        return 0.0
    return round(v, digits - 1 - math.floor(math.log10(abs(v))))


def _nice_scales(scale: float):
    """Tidier stand-ins for a least-squares scale, simplest first, each with
    its rank: 1-4 significant figures, then 1/2^n (1/256 etc.)."""
    for digits in range(1, 5):
        yield digits, _sig_round(scale, digits)
    sign = 1.0 if scale > 0 else -1.0
    n = round(-math.log2(abs(scale)))
    if 0 < n <= 16:
        yield 2, sign * 2.0 ** -n


@dataclass
class Fit:
    """One field's least-squares fit. When `problem` is set no fit was
    possible and the numbers are meaningless."""

    offset: float = 0.0
    scale: float = 0.0
    used: list[int] = field(default_factory=list)  # indexes into the points
    raws: list[float] = field(default_factory=list)  # decoded, per used point
    residuals: list[float] = field(default_factory=list)  # true - fitted
    r2: float = 0.0
    tolerance: float = 0.0
    scale_digits: int = 5  # significant figures the scale needed; 5 = untidy
    problem: str = ""

    @property
    def ok(self) -> bool:
        return not self.problem

    @property
    def max_error(self) -> float:
        return max((abs(r) for r in self.residuals), default=0.0)

    @property
    def worst(self) -> int | None:
        """The point (index into the points) furthest from the line."""
        if not self.residuals:
            return None
        i = max(range(len(self.residuals)), key=lambda k: abs(self.residuals[k]))
        return self.used[i]

    @property
    def exact(self) -> bool:
        """Every point within display rounding of the line."""
        return self.ok and self.max_error <= self.tolerance

    @property
    def confirmed(self) -> bool:
        return self.exact and len(self.used) >= CONFIRM_POINTS

    def summary(self) -> str:
        if self.problem:
            return self.problem
        n = len(self.used)
        if self.confirmed:
            return (f"fits all {n} points within display rounding "
                    f"(±{self.tolerance:.3g})")
        if self.exact:
            return ("a line always fits 2 points -- add a third to confirm "
                    "it's a straight-line scale")
        return (f"off by up to {self.max_error:.3g} (point {self.worst + 1}, "
                f"r² {self.r2:.4f}) -- wrong bytes, a mis-paired point, "
                f"or not a straight-line scale")


def _numeric(points: list[Point]) -> list[int]:
    return [i for i, p in enumerate(points) if p.value is not None]


def _residuals(raws, values, offset, scale) -> list[float]:
    return [v - (r + offset) * scale for r, v in zip(raws, values)]


def _fit_raws(raws: list[float], values: list[float], used: list[int]) -> Fit:
    n = len(raws)
    tol = display_tolerance(values)
    if n < 2:
        return Fit(problem=f"needs 2+ points with a true value (has {n})")
    mr, mv = sum(raws) / n, sum(values) / n
    srr = sum((r - mr) ** 2 for r in raws)
    svv = sum((v - mv) ** 2 for v in values)
    if srr == 0:
        return Fit(problem="every point decodes to the same raw value "
                           "from these bytes")
    if svv == 0:
        return Fit(problem="every point has the same true value, so there's "
                           "no scale to find")
    scale = sum((r - mr) * (v - mv) for r, v in zip(raws, values)) / srr
    if scale == 0:
        return Fit(problem="the true value doesn't follow these bytes at all")
    offset = mv / scale - mr  # line through the means: v = (r + off) * s
    res = _residuals(raws, values, offset, scale)
    digits = 5

    # Swap in tidy numbers (0.1 rather than 0.100013, -40 rather than
    # -39.98) when they still match every point -- they're nearly always
    # what the module actually uses.
    if max(abs(x) for x in res) <= tol:
        for rank, s in _nice_scales(scale):
            off = sum(v / s - r for r, v in zip(raws, values)) / n
            if max(abs(x) for x in _residuals(raws, values, off, s)) <= tol:
                scale, offset, digits = s, off, rank
                break
        for places in range(0, 5):
            off = round(offset, places)
            if max(abs(x) for x in _residuals(raws, values, off, scale)) <= tol:
                offset = off
                break
        res = _residuals(raws, values, offset, scale)

    offset += 0.0  # -0.0 -> 0.0
    r2 = 1.0 - sum(x * x for x in res) / svv
    return Fit(offset, scale, used, raws, res, r2, tol, digits)


def fit_points(points: list[Point], fld: Field) -> Fit:
    """Least-squares `value = (decode_field(payload, fld) + offset) * scale`
    over the points that have a true value (labelled-only points are
    skipped)."""
    used = _numeric(points)
    raws = []
    for i in used:
        r = decode_field(points[i].payload_bytes, fld)
        if r is None:
            return Fit(problem=f"point {i + 1}'s payload is too short for "
                               f"{fld.describe()}")
        raws.append(r)
    return _fit_raws(raws, [points[i].value for i in used], used)


@dataclass
class Candidate:
    """A field the search found, with its fit (or enum mapping), and any
    other fields that explain the points exactly as well -- e.g. u8 @ byte 1
    and u16 @ byte 0 when byte 0 never changed."""

    field: Field
    fit: Fit | None = None
    mapping: "Mapping | None" = None
    also: list[Field] = field(default_factory=list)


def _byte_fields(min_len: int, max_len: int = 4) -> list[Field]:
    out = []
    for length in range(1, max_len + 1):
        for start in range(0, min_len - length + 1):
            for little in (False, True) if length > 1 else (False,):
                out.append(Field(start, length, little_endian=little))
                out.append(Field(start, length, signed=True, little_endian=little))
    return out


# How many spare bits a value may share its bytes with -- status flags in
# the top bits (battery voltage, 2A 07: 14 bits under 2 flag bits) or the
# bottom ones.
_MAX_TRIMMED_BITS = 7


def _trimmed_fields(min_len: int, max_len: int = 4) -> list[Field]:
    """Every byte-aligned field again, with 1-7 bits dropped off the top or
    the bottom: an unsigned value that doesn't fill its bytes."""
    out = []
    for length in range(1, max_len + 1):
        width = length * 8
        for start in range(0, min_len - length + 1):
            for little in (False, True) if length > 1 else (False,):
                for drop in range(1, min(_MAX_TRIMMED_BITS, width - 1) + 1):
                    out.append(Field(start, length, bit_shift=0,
                                     bit_len=width - drop, little_endian=little))
                    out.append(Field(start, length, bit_shift=drop,
                                     bit_len=width - drop, little_endian=little))
    return out


def _simplicity(f: Field) -> tuple:
    # Whole bytes before bit fields, short before long, big-endian before
    # little, unsigned before signed, early before late; of bit fields, the
    # fewest bits dropped.
    return (f.bit_len != 0, f.length, f.little_endian, f.signed, f.start,
            -f.bit_len, f.bit_shift)


def _decoded(points: list[Point], used: list[int], f: Field) -> tuple:
    return tuple(decode_field(points[i].payload_bytes, f) for i in used)


def _affine_key(raws: tuple) -> tuple:
    # Fields whose raws are a straight-line function of each other fit the
    # points equally well; with 2 points every pair is, so only group
    # identical raws there.
    if len(raws) < CONFIRM_POINTS:
        return raws
    base = raws[0]
    step = next((r - base for r in raws if r != base), 0) or 1
    return tuple(round((r - base) / step, 9) for r in raws)


def search_fields(points: list[Point], max_len: int = 4) -> list[Candidate]:
    """Every byte-aligned field (start x 1-4 bytes x signed x byte order),
    and each with a few high or low bits masked off, fitted against
    the points, best first: fits within rounding before ones that don't,
    then the tidiest scale, then the simplest field. Fields that can't fit
    at all (constant bytes, payload too short) are left out.

    With only 2 points every varying field fits exactly, so the order there
    rests on scale tidiness alone -- a guess until there's a third point."""
    used = _numeric(points)
    if len(used) < 2:
        return []
    min_len = min(len(points[i].payload_bytes) for i in used)
    values = [points[i].value for i in used]

    groups: dict[tuple, list[Candidate]] = {}
    for f in _byte_fields(min_len, max_len) + _trimmed_fields(min_len, max_len):
        raws = _decoded(points, used, f)
        fit = _fit_raws(list(raws), values, used)
        if fit.ok:
            groups.setdefault(_affine_key(raws), []).append(Candidate(f, fit))

    out = []
    for members in groups.values():
        # Fields that read the points identically: whole bytes win even over
        # a tidier scale -- dropping the low bit of all-even raws halves the
        # scale, and that's a coincidence of the points, not a better field.
        members.sort(key=lambda c: (c.field.bit_len != 0, c.fit.scale_digits,
                                    _simplicity(c.field)))
        best = members[0]
        best.also = [c.field for c in members[1:]]
        out.append(best)

    def rank(c: Candidate):
        if c.fit.exact:
            return (0, c.fit.scale_digits, 0.0, _simplicity(c.field))
        return (1, 0, -c.fit.r2, _simplicity(c.field))

    out.sort(key=rank)
    return out


@dataclass
class Mapping:
    """Which raw value each label came from, for an Enum signal. When
    `problem` is set no mapping was possible."""

    entries: list[EnumEntry] = field(default_factory=list)
    conflicts: list[str] = field(default_factory=list)
    used: list[int] = field(default_factory=list)
    labels: int = 0  # distinct labels among the points
    problem: str = ""

    @property
    def ok(self) -> bool:
        return not self.problem and not self.conflicts

    @property
    def extra_values(self) -> int:
        """Raw values beyond one per label. 0 = each label has exactly one
        raw value; a field that differs at every point (a counter) "maps"
        trivially but scores badly here."""
        return len({e.value for e in self.entries}) - self.labels

    def summary(self) -> str:
        if self.problem:
            return self.problem
        if self.conflicts:
            return "; ".join(self.conflicts)
        pairs = ", ".join(f"{e.value}={e.label}" for e in self.entries)
        return f"consistent over {len(self.used)} points: {pairs}"


def _labelled(points: list[Point]) -> list[int]:
    return [i for i, p in enumerate(points) if p.label.strip()]


def _map_raws(raws, labels, used) -> Mapping:
    if len(used) < 1:
        return Mapping(problem="needs points with a label")
    seen: dict[int, list[str]] = {}
    for r, label in zip(raws, labels):
        names = seen.setdefault(int(r), [])
        if label not in names:
            names.append(label)
    entries = [EnumEntry(v, names[0]) for v, names in sorted(seen.items())]
    conflicts = [f"raw {v} was seen as " + " and ".join(repr(n) for n in names)
                 for v, names in sorted(seen.items()) if len(names) > 1]
    return Mapping(entries, conflicts, used, len(set(labels)))


def fit_enum(points: list[Point], fld: Field) -> Mapping:
    """The raw value -> label table the labelled points imply for `fld`,
    or the raw values that were seen under more than one label."""
    used = _labelled(points)
    raws = []
    for i in used:
        r = decode_field(points[i].payload_bytes, fld)
        if r is None:
            return Mapping(problem=f"point {i + 1}'s payload is too short for "
                                   f"{fld.describe()}")
        raws.append(r)
    return _map_raws(raws, [points[i].label.strip() for i in used], used)


def _enum_fields(min_len: int) -> list[Field]:
    out = [Field(s, n) for n in (1, 2) for s in range(0, min_len - n + 1)]
    out += [Field(s, 2, little_endian=True) for s in range(0, min_len - 1)]
    for s in range(min_len):
        out += [Field(s, 1, bit_shift=b, bit_len=1) for b in range(8)]
        out += [Field(s, 1, bit_shift=b, bit_len=4) for b in (0, 4)]
    return out


def search_enum_fields(points: list[Point]) -> list[Candidate]:
    """Every byte, 2-byte, nibble and single-bit field checked against the
    labelled points, best first: no conflicts, then the fewest raw values
    per label, then the simplest field. Needs points with at least two
    different labels -- with one, every field is trivially consistent."""
    used = _labelled(points)
    labels = [points[i].label.strip() for i in used]
    if len(set(labels)) < 2:
        return []
    min_len = min(len(points[i].payload_bytes) for i in used)

    groups: dict[tuple, list[Candidate]] = {}
    for f in _enum_fields(min_len):
        raws = _decoded(points, used, f)
        if len(set(raws)) < 2:
            continue
        # Fields that split the points the same way are the same answer.
        first: dict[float, int] = {}
        key = tuple(first.setdefault(r, len(first)) for r in raws)
        m = _map_raws(raws, labels, used)
        groups.setdefault(key, []).append(Candidate(f, mapping=m))

    out = []
    for members in groups.values():
        members.sort(key=lambda c: _simplicity(c.field))
        best = members[0]
        best.also = [c.field for c in members[1:]]
        out.append(best)
    out.sort(key=lambda c: (len(c.mapping.conflicts), c.mapping.extra_values,
                            _simplicity(c.field)))
    return out
