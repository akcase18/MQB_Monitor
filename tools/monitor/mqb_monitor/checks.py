"""Unit checks for the headless --selftest: decode, registry schema and
round-trip. Each check is a plain function that raises AssertionError on
failure; run_checks() runs them all and reports by name. No PySide6."""
from __future__ import annotations

import importlib.util
import json
import tempfile
from pathlib import Path

from .can_frame import CanFrame
from .capture import CaptureWriter, new_capture_path, read_capture
from .catalog import (
    AUTO_FIELD,
    REPO_ROOT,
    ENGINE_ECU,
    UNITS,
    Catalog,
    Ecu,
    EnumEntry,
    Field,
    Point,
    Request,
    Signal,
    _field_from_json,
    _request_to_json,
    decode_field,
    decode_raw_payload,
    encode_field,
    field_range,
    signal_to_json,
    simplest_field,
    to_imperial,
)
from .session import MAX_DISTINCT_SAMPLES, MonitorEngine, fill_wip_ecus, frame_note
from .solver import (
    fit_enum,
    fit_points,
    search_enum_fields,
    search_fields,
)


def _legacy_decode(payload: bytes) -> float:
    """The decode as it was before byte fields existed -- the firmware's
    UdsClient::poll(), verbatim."""
    if len(payload) == 0:
        return 0.0
    if len(payload) == 1:
        return float(payload[0])
    v = (payload[0] << 8) | payload[1]
    if v >= 0x8000:
        v -= 0x10000
    return float(v)


def check_auto_field_matches_legacy() -> None:
    for payload in (b"", b"\x7f", b"\xff", b"\x00\x01", b"\xff\xfe",
                    b"\x80\x00", b"\x12\x34\x56", b"\x00\x00\x00\x7f\xff"):
        assert decode_field(payload) == _legacy_decode(payload), payload.hex()
        assert decode_raw_payload(payload) == _legacy_decode(payload)


def check_explicit_fields() -> None:
    p = bytes.fromhex("0308240004")  # cluster dimming, 0x1100
    assert decode_field(p, Field(start=2, length=1)) == 0x24
    assert decode_field(p, Field(start=0, length=2)) == 0x0308
    assert decode_field(p, Field(start=1, length=4)) == 0x08240004
    assert decode_field(p, Field(start=4, length=2)) is None  # too short
    s = bytes.fromhex("f83000")  # steering torque, signed
    assert decode_field(s, Field(start=0, length=2, signed=True)) == -2000
    assert decode_field(s, Field(start=0, length=2)) == 0xF830
    # Bit sub-field: the high nibble of byte 0, signed at 4 bits.
    assert decode_field(b"\xa5", Field(0, 1, bit_shift=4, bit_len=4)) == 0xA
    assert decode_field(b"\xa5", Field(0, 1, True, 4, 4)) == -6
    assert decode_field(b"\xa5", Field(0, 1, bit_shift=0, bit_len=1)) == 1
    # Little-endian: low byte first (clutch torque, DID 38 2E).
    le = Field(0, 2, little_endian=True)
    assert decode_field(bytes.fromhex("1000"), le) == 0x0010
    assert decode_field(bytes.fromhex("f8ff"), Field(0, 2, True, little_endian=True)) == -8
    assert encode_field(bytes(2), le, 0x1234) == bytes.fromhex("3412")
    assert le.describe() == "u16 LE @ byte 0"
    req = Request(0x38, 0x2E, 0.0, 0.05, field=le)
    back = _field_from_json(_request_to_json(req)["field"])
    assert back == le, back
    assert "little_endian" not in _request_to_json(Request(0x38, 0x2E, 0, 1, field=Field(0, 2)))["field"]


def check_request_decode_and_ecu_match() -> None:
    req = Request(0x18, 0x10, 0.0, 0.01, ecu=Ecu(0x713, 0x77D),
                  field=Field(start=0, length=2))
    assert abs(req.decode(bytes.fromhex("0c9e")) - 32.30) < 1e-9
    sig = Signal(1000, "T", "Pressure", 100, "default", 0, 100, [req])
    cat = Catalog([sig])
    assert cat.signals_for_did((0x18, 0x10)) == [(sig, 0)]
    assert cat.signals_for_did((0x18, 0x10), rx_id=0x77D) == [(sig, 0)]
    assert cat.signals_for_did((0x18, 0x10), rx_id=0x7E8) == []
    # A request with no ECU yet matches any responder.
    sig.requests[0].ecu = None
    cat._reindex()
    assert cat.signals_for_did((0x18, 0x10), rx_id=0x7E8) == [(sig, 0)]


def check_units() -> None:
    keys = [u.key for u in UNITS]
    # Wire ordinals the phone page depends on -- the first nine never move.
    assert keys[:9] == ["None", "Angle", "Percent", "Pressure", "RPM",
                        "Speed", "Temp", "Torque", "Volume"], keys
    assert abs(to_imperial("Length", 25.4) - 1.0) < 1e-9
    assert abs(to_imperial("TempDelta", 10.0) - 18.0) < 1e-9
    assert abs(to_imperial("Temp", 10.0) - 50.0) < 1e-9


def check_enum_label() -> None:
    sig = Signal(1, "Gear", "Enum", 100, "default", 0, 0, [],
                 enum=[EnumEntry(0, "1st"), EnumEntry(8, "Shifting")])
    assert sig.enum_label(0.0) == "1st"
    assert sig.enum_label(8.0) == "Shifting"
    assert sig.enum_label(3.0) == "?3"
    assert sig.format_value(8.0) == "Shifting"


def check_json_round_trip() -> None:
    sig = Signal(
        5, "X", "Voltage", 100, "default", 0, 16,
        [Request(0x2A, 0x07, 0.0, 0.001, ecu=ENGINE_ECU,
                 field=Field(1, 2, True, 3, 5))],
        status="wip",
        enum=[EnumEntry(1, "On")],
        points=[Point("28bf", 12.5, note="idle", source="manual", at=1.0),
                Point("00", None, label="Off", source="live", at=2.0)],
    )
    legacy = Signal(6, "Y", "Temp", 100, "default", 0, 100,
                    [Request(0x20, 0x2B, -40.0, 1.0)])
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "signals.json"
        path.write_text(json.dumps({"signals": []}))
        Catalog([sig, legacy], path=path).save()
        raw = json.loads(path.read_text())
        # A legacy request writes no optional keys at all.
        assert set(raw["signals"][1]["requests"][0]) == {
            "byte1", "byte2", "offset", "scale"}
        assert "enum" not in raw["signals"][1]
        back = Catalog.load(path).signals
    assert back[0].requests[0] == sig.requests[0]
    assert back[0].enum == sig.enum and back[0].points == sig.points
    assert back[1].requests[0].ecu is None
    assert back[1].requests[0].field == AUTO_FIELD


def check_degrees_alias() -> None:
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "signals.json"
        path.write_text(json.dumps({"signals": [{
            "id": 0, "name": "A", "unit": "Degrees", "cycle_time_ms": 100,
            "display_min": 0, "display_max": 1,
            "requests": [{"byte1": "0x01", "byte2": "0x02",
                          "offset": 0, "scale": 1}]}]}))
        assert Catalog.load(path).signals[0].unit == "Angle"


def check_registry_loads() -> None:
    cat = Catalog.load()
    keys = {u.key for u in UNITS}
    for sig in cat.signals:
        assert sig.unit in keys, (sig.id, sig.unit)
        if sig.in_firmware:  # the firmware has to know who to ask
            for req in sig.requests:
                assert req.ecu is not None, sig.id


def _req_frame(did: tuple[int, int]) -> bytes:
    return bytes([0x03, 0x22, *did, 0, 0, 0, 0])


def _resp_frame(did: tuple[int, int], payload: bytes) -> bytes:
    data = bytes([3 + len(payload), 0x62, *did]) + payload
    return data + bytes(8 - len(data))


def check_unknown_did_records_ecu() -> None:
    engine = MonitorEngine(Catalog([]))
    did = (0x2B, 0xE6)
    engine.feed(CanFrame(1.0, 0x70F, _req_frame(did)))
    engine.feed(CanFrame(1.01, 0x779, _resp_frame(did, b"\x00\x0d")))
    u = engine.snapshot().unknown[(0x779, did)]
    assert u.ecu == Ecu(0x70F, 0x779), u.ecu
    # A response with no request seen still records who answered.
    engine.feed(CanFrame(2.0, 0x77D, _resp_frame((0x18, 0x10), b"\x05")))
    u = engine.snapshot().unknown[(0x77D, (0x18, 0x10))]
    assert (u.tx_id, u.ecu) == (None, None)
    # The same DID from a second module is a second entry, and when both
    # requests are pending, each response pairs with its own module's.
    engine.feed(CanFrame(3.0, 0x7E1, _req_frame(did)))
    engine.feed(CanFrame(3.001, 0x70F, _req_frame(did)))
    engine.feed(CanFrame(3.01, 0x7E9, _resp_frame(did, b"\x01")))
    engine.feed(CanFrame(3.02, 0x779, _resp_frame(did, b"\x00\x0e")))
    snap = engine.snapshot()
    assert [u.ecu for u in snap.responders(did)] == [
        Ecu(0x70F, 0x779), Ecu(0x7E1, 0x7E9)], snap.responders(did)
    assert snap.unknown[(0x779, did)].count == 2


def check_distinct_samples() -> None:
    engine = MonitorEngine(Catalog([]))
    did = (0x11, 0x00)
    for i, payload in enumerate([b"\x01", b"\x02", b"\x01", b"\x01", b"\x03"]):
        engine.feed(CanFrame(float(i), 0x77E, _resp_frame(did, payload)))
    u = engine.snapshot().unknown[(0x77E, did)]
    assert u.count == 5 and u.last == b"\x03"
    # Least recently seen first; counts kept per payload.
    assert list(u.distinct.items()) == [
        (b"\x02", 1), (b"\x01", 3), (b"\x03", 1)], u.distinct
    for i in range(MAX_DISTINCT_SAMPLES + 10):
        engine.feed(CanFrame(10.0 + i, 0x77E,
                             _resp_frame(did, i.to_bytes(2, "big"))))
    u = engine.snapshot().unknown[(0x77E, did)]
    assert len(u.distinct) == MAX_DISTINCT_SAMPLES
    # The snapshot is a copy: more traffic doesn't change it under the GUI.
    engine.feed(CanFrame(99.0, 0x77E, _resp_frame(did, b"\xff")))
    assert u.last != b"\xff"


def check_fill_wip_ecus() -> None:
    def wip(sid: int, did: tuple[int, int], ecu=None) -> Signal:
        return Signal(sid, f"s{sid}", "None", 100, "default", 0, 1,
                      [Request(*did, 0.0, 1.0, ecu=ecu)], status="wip")

    one, two, no_tx, done = (0x2B, 0xE6), (0x18, 0x10), (0x11, 0x00), (0x22, 0x01)
    sigs = [wip(1, one), wip(2, two), wip(3, no_tx),
            wip(4, done, ecu=Ecu(0x7E0, 0x7E8)), wip(5, (0x99, 0x99))]
    cat = Catalog(sigs)
    engine = MonitorEngine(cat)
    for t, (tx, rx, did) in enumerate([
        (0x70F, 0x779, one),
        (0x713, 0x77D, two), (0x7E0, 0x7E8, two),  # two modules answer
        (None, 0x77E, no_tx),  # request never seen
        (0x70F, 0x779, done),
    ]):
        if tx is not None:
            engine.feed(CanFrame(t, tx, _req_frame(did)))
        engine.feed(CanFrame(t + 0.01, rx, _resp_frame(did, b"\x01")))
    changed = fill_wip_ecus(cat, engine.snapshot().unknown)
    assert [s.id for s in changed] == [1], [s.id for s in changed]
    assert sigs[0].requests[0].ecu == Ecu(0x70F, 0x779)
    assert sigs[1].requests[0].ecu is None and sigs[2].requests[0].ecu is None
    assert sigs[3].requests[0].ecu == Ecu(0x7E0, 0x7E8)  # never overwritten
    assert fill_wip_ecus(cat, engine.snapshot().unknown) == []
    # Lookups by module: a WIP signal only claims its own module's DID.
    assert cat.find_by_did(one, rx_id=0x779) == [sigs[0]]
    assert cat.find_by_did(one, rx_id=0x7E8) == []
    assert cat.find_by_did(two, rx_id=0x7E8) == [sigs[1]]  # ECU still unknown


def check_firmware_validation() -> None:
    spec = importlib.util.spec_from_file_location(
        "gen_catalog", REPO_ROOT / "tools/gen_catalog.py")
    gen = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gen)

    def problems(sig: Signal) -> list[str]:
        return gen.validate({"signals": [signal_to_json(sig)]})

    ok = Signal(0, "A", "Temp", 100, "default", 0, 1,
                [Request(0x20, 0x2B, 0.0, 1.0, ecu=ENGINE_ECU)])
    assert problems(ok) == []
    no_ecu = Signal(0, "A", "Temp", 100, "default", 0, 1,
                    [Request(0x20, 0x2B, 0.0, 1.0)])
    assert any("no ECU" in p for p in problems(no_ecu))
    other = Signal(0, "A", "Temp", 100, "default", 0, 1,
                   [Request(0x2B, 0xE6, 0.0, 1.0, ecu=Ecu(0x70F, 0x779))])
    assert problems(other) == []  # any module compiles now
    # ...but two modules can't answer on the same id.
    clash = Signal(1, "B", "Temp", 100, "default", 0, 1,
                   [Request(0x2B, 0xE7, 0.0, 1.0, ecu=Ecu(0x710, 0x779))])
    both = gen.validate({"signals": [signal_to_json(other), signal_to_json(clash)]})
    assert any("share response id 0x779" in p for p in both), both
    wide = Signal(0, "C", "Temp", 100, "default", 0, 1,
                  [Request(0x2B, 0xE6, 0.0, 1.0, ecu=Ecu(0x18DA10F1, 0x779))])
    assert any("11-bit" in p for p in problems(wide))
    many = [signal_to_json(Signal(i, f"S{i}", "Temp", 100, "default", 0, 1,
                                  [Request(0x20, i, 0.0, 1.0, ecu=ENGINE_ECU)]))
            for i in range(65)]
    assert any("room for 64" in p for p in gen.validate({"signals": many}))
    assert gen.validate({"signals": many[:64]}) == []
    enum = Signal(0, "G", "Enum", 100, "default", 0, 1,
                  [Request(0x21, 0x0F, 0.0, 1.0, ecu=ENGINE_ECU)])
    assert any("no enum entries" in p for p in problems(enum))
    enum.enum = [EnumEntry(0, "1st"), EnumEntry(8, "Shifting")]
    assert problems(enum) == []  # Enums compile now
    enum.enum = [EnumEntry(0, "A much too long label")]
    assert any("1-15 characters" in p for p in problems(enum))
    # A name past one line of a small panel wraps into a Gauge; a state is
    # always Digital, so it may use the second line.
    ok.name = "x" * 17
    assert problems(ok) == []
    ok.name = "x" * 18
    assert any("wraps into a Gauge" in p for p in problems(ok))
    enum.enum = [EnumEntry(0, "1st")]
    enum.name = "x" * 23
    assert problems(enum) == []
    enum.name = "x" * 24
    assert any("cut off" in p for p in problems(enum))
    twin = Signal(2, other.name, "Percent", 100, "default", 0, 1,
                  [Request(0x2B, 0xE7, 0.0, 1.0, ecu=Ecu(0x70F, 0x779))])
    pair = gen.validate({"signals": [signal_to_json(other), signal_to_json(twin)]})
    assert any("both named" in p for p in pair), pair
    enum.enum = [EnumEntry(0, "1°")]
    assert any("plain ASCII" in p for p in problems(enum))
    enum.enum = [EnumEntry(1, "a"), EnumEntry(1, "b")]
    assert any("listed twice" in p for p in problems(enum))
    fld = Signal(0, "D", "Voltage", 100, "default", 0, 1,
                 [Request(0x11, 0x00, 0.0, 1.0, ecu=ENGINE_ECU,
                          field=Field(2, 1))])
    assert problems(fld) == []  # byte fields and the newer units too
    fld.requests[0].field = Field(0, 1, bit_shift=6, bit_len=4)
    assert any("bit range" in p for p in problems(fld))
    # WIP signals are never checked.
    assert gen.validate({"signals": [
        {**signal_to_json(enum), "status": "wip"}]}) == []
    # Internal ones are, like active ones; an unknown status is refused.
    assert any("listed twice" in p for p in gen.validate({"signals": [
        {**signal_to_json(enum), "status": "internal"}]}))
    assert any("unknown status" in p for p in gen.validate({"signals": [
        {**signal_to_json(ok), "status": "retired"}]}))


def check_internal_signals() -> None:
    """Internal signals are compiled after every active one, outside
    SIGNAL_CATALOG_COUNT, kept off the phone page, and decoded live by the
    monitor like active ones."""
    spec = importlib.util.spec_from_file_location(
        "gen_catalog", REPO_ROOT / "tools/gen_catalog.py")
    gen = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gen)

    def sig(sid: int, name: str, status: str, did: int) -> Signal:
        s = Signal(sid, name, "Percent", 100, "default", 0, 100,
                   [Request(0x11, did, 0.0, 1.0, ecu=Ecu(0x714, 0x77E))])
        s.status = status
        return s

    # Registry order: an internal one between two active ones.
    a, i, b, w = (sig(0, "A", "active", 1), sig(1, "Dim", "internal", 2),
                  sig(2, "B", "active", 3), sig(3, "W", "wip", 4))
    data = {"signals": [signal_to_json(s) for s in (a, i, b, w)]}
    assert [s["id"] for s in gen.firmware_signals(data)] == [0, 2, 1]
    assert [s["id"] for s in gen.active_signals(data)] == [0, 2]
    out = gen.build(data)
    assert "SIGNAL_CATALOG_JSON_COUNT = 2;" in out, out
    assert "INTERNAL_SIGNAL_JSON_COUNT = 1;" in out, out
    assert out.index("/* B */") < out.index("Internal:") < out.index("/* Dim */")
    # Internal names collide with active ones like any two compiled signals.
    twin = sig(4, "A", "internal", 5)
    clash = gen.validate({"signals": [signal_to_json(a), signal_to_json(twin)]})
    assert any("both named" in p for p in clash), clash

    cat = Catalog([a, i, w])
    assert i.in_firmware and a.in_firmware and not w.in_firmware
    assert cat.signals_for_did((0x11, 2)) == [(i, 0)]
    assert cat.signals_for_did((0x11, 4)) == []


def _pt(payload: bytes, value=None, label="") -> Point:
    return Point(payload.hex(), value, label=label)


def _packed(counter: int, raw: int, noise: int) -> bytes:
    # A made-up multi-value DID: [counter][value u16][constant 0x5A][noise]
    return bytes([counter & 0xFF]) + raw.to_bytes(2, "big") + bytes([0x5A, noise])


def check_fit_recovers_scale() -> None:
    # Phone shows (raw - 40) * 0.1 rounded to 1 decimal.
    raws = [400, 655, 1010, 1503, 2222]
    pts = [_pt(_packed(i * 37, r, (i * 91) % 256), round((r - 40) * 0.1, 1))
           for i, r in enumerate(raws)]
    fit = fit_points(pts, Field(1, 2))
    assert fit.confirmed, fit.summary()
    assert (fit.scale, fit.offset) == (0.1, -40.0), (fit.scale, fit.offset)
    # The same points against the wrong bytes don't fit.
    assert not fit_points(pts, Field(0, 1)).exact
    # Labelled-only points are skipped, not treated as zero.
    assert fit_points(pts + [_pt(_packed(0, 9, 0), label="x")],
                      Field(1, 2)).confirmed


def check_fit_reports_what_it_cant_do() -> None:
    two = [_pt(b"\x10", 1.0), _pt(b"\x20", 2.0)]
    fit = fit_points(two, Field(0, 1))
    assert fit.exact and not fit.confirmed and "third" in fit.summary()
    assert "2+ points" in fit_points(two[:1], Field(0, 1)).problem
    same_raw = [_pt(b"\x10", 1.0), _pt(b"\x10", 2.0)]
    assert "same raw" in fit_points(same_raw, Field(0, 1)).problem
    assert "too short" in fit_points(two, Field(0, 2)).problem
    # Not a straight line (a lookup table): fitted, but flagged, with the
    # worst point named.
    curve = [_pt(bytes([r]), r * r / 10) for r in (1, 5, 10, 20, 40)]
    fit = fit_points(curve, Field(0, 1))
    assert fit.ok and not fit.exact and fit.worst is not None
    assert "not a straight-line" in fit.summary()


def check_search_finds_packed_field() -> None:
    raws = [400, 655, 1010, 1503, 2222]
    pts = [_pt(_packed(i * 37, r, (i * 91) % 256), round((r - 40) * 0.1, 1))
           for i, r in enumerate(raws)]
    best = search_fields(pts)[0]
    assert best.field == Field(1, 2), best.field.describe()
    assert best.fit.confirmed and best.fit.scale == 0.1
    # A signed value: steering torque in s16 @ byte 2 of 4.
    vals = [-2000, -350, 0, 420, 1800]
    pts = [_pt(bytes([7, i]) + (v & 0xFFFF).to_bytes(2, "big"), v / 100)
           for i, v in enumerate(vals)]
    best = search_fields(pts)[0]
    assert best.field == Field(2, 2, signed=True), best.field.describe()
    assert best.fit.scale == 0.01
    # A u8 value whose neighbour never changed: the u16 reading of the same
    # bytes is offered as an equally good alternative.
    pts = [_pt(bytes([0, r]), r * 0.5) for r in (10, 60, 130, 200)]
    best = search_fields(pts)[0]
    assert best.field == Field(1, 1) and Field(0, 2) in best.also, best
    assert search_fields(pts[:1]) == []
    # Flag bits on top: battery voltage (2A 07) is 14 bits under 2 others,
    # 0.001 V a step from 4 V -- the bench points that had no fit before.
    pts = [_pt(bytes.fromhex(h), v) for h, v in (
        ("0000", 4.0), ("0001", 4.001), ("0010", 4.016), ("2010", 12.208),
        ("f010", 16.304))]
    best = search_fields(pts)[0]
    assert best.field == Field(0, 2, bit_shift=0, bit_len=14), best.field.describe()
    assert best.fit.confirmed and abs(best.fit.scale - 0.001) < 1e-12
    assert abs(best.fit.offset - 4000) < 1e-6
    # All-even raws: dropping the low bit reads them identically with a
    # tidier scale, but the whole byte still wins.
    pts = [_pt(bytes([r]), r * 0.25) for r in (8, 40, 100, 200)]
    best = search_fields(pts)[0]
    assert best.field == Field(0, 1), best.field.describe()
    # Little-endian: what the phone showed for clutch torque on the bench.
    pts = [_pt(bytes.fromhex(h), v) for h, v in (("1111", 218.45), ("1000", 0.8),
                                                    ("1100", 0.85))]
    best = search_fields(pts)[0]
    assert best.field == Field(0, 2, little_endian=True), best.field.describe()
    assert best.fit.confirmed and abs(best.fit.scale - 0.05) < 1e-12


def check_enum_mapping() -> None:
    # Gear from a byte: 0 = 1st, 1 = 2nd, 8 = Shifting.
    pts = [_pt(bytes([0x22, g]), label=l)
           for g, l in [(0, "1st"), (1, "2nd"), (8, "Shifting"), (0, "1st")]]
    m = fit_enum(pts, Field(1, 1))
    assert m.ok and [(e.value, e.label) for e in m.entries] == [
        (0, "1st"), (1, "2nd"), (8, "Shifting")], m.summary()
    bad = pts + [_pt(bytes([0x22, 1]), label="3rd")]
    assert not fit_enum(bad, Field(1, 1)).ok
    assert "raw 1" in fit_enum(bad, Field(1, 1)).summary()
    # A valve flag in bit 3 of byte 1, every other bit noise.
    flags = [(0x08 | 0x41, "Open"), (0x00 | 0x13, "Closed"),
             (0x08 | 0x80, "Open"), (0x00 | 0x05, "Closed"),
             (0x08 | 0x26, "Open")]
    counter = [5, 6, 9, 13, 2]  # no bit of it follows the flag by chance
    pts = [_pt(bytes([c, f, 0xFF]), label=l)
           for c, (f, l) in zip(counter, flags)]
    best = search_enum_fields(pts)[0]
    assert best.field == Field(1, 1, bit_shift=3, bit_len=1), best.field
    assert best.mapping.ok and best.mapping.extra_values == 0
    assert search_enum_fields(pts[:1]) == []  # one label proves nothing


def check_simplest_field() -> None:
    two = [b"\x01\x02", b"\xff\x00\x07"]
    assert simplest_field(Field(0, 2, signed=True), two) == AUTO_FIELD
    assert simplest_field(Field(0, 2), two) == Field(0, 2)  # unsigned differs
    assert simplest_field(Field(0, 1), [b"\x05", b"\x06"]) == AUTO_FIELD
    assert simplest_field(Field(0, 1), [b"\x05\x06"]) == Field(0, 1)
    assert simplest_field(Field(0, 2, True), [b"\x05"]) == Field(0, 2, True)
    assert simplest_field(Field(2, 1), []) == Field(2, 1)


def check_engine_latest() -> None:
    did = (0x11, 0x00)
    active = Signal(1, "A", "None", 100, "default", 0, 1,
                    [Request(*did, 0.0, 1.0, ecu=Ecu(0x714, 0x77E))])
    engine = MonitorEngine(Catalog([active]))
    assert engine.latest(did) is None
    # Known responses count too, not just unknown ones.
    engine.feed(CanFrame(1.0, 0x77E, _resp_frame(did, b"\x03\x08\x24")))
    assert engine.latest(did) == (b"\x03\x08\x24", 1.0, 0x77E)
    engine.feed(CanFrame(2.0, 0x7E8, _resp_frame(did, b"\x09")))
    assert engine.latest(did) is None  # two modules, no way to tell
    assert engine.latest(did, rx_id=0x7E8)[0] == b"\x09"


def check_capture_round_trip() -> None:
    import time as _time
    from datetime import datetime

    from .sources import DemoSource, FileSource

    assert new_capture_path("demo", datetime(2026, 10, 29, 18, 4, 11)).name \
        == "2026-10-29_180411_demo.csv"
    cat = Catalog.load()
    live = MonitorEngine(cat)
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "cap.csv"
        writer = CaptureWriter(path, "demo")

        def on_frame(frame):
            live.feed(frame)
            writer.write(frame, frame_note(frame))

        demo = DemoSource(cat, on_frame, period_s=0.002)
        demo.start()
        _time.sleep(0.4)
        demo.stop()
        writer.close()

        info, frames = read_capture(path)
        assert info.is_demo and len(frames) == writer.count > 0
        replayed = MonitorEngine(cat)
        src = FileSource(path, replayed.feed, realtime=False)
        replayed.reset(started_at=src.base)
        src.start()
        for _ in range(200):
            if src.done:
                break
            _time.sleep(0.01)
        assert src.done and src.sent == len(frames)

        a, b = live.snapshot(), replayed.snapshot()
        assert a.total_frames == b.total_frames
        assert {k: r.value for k, r in a.readings.items()} == \
            {k: r.value for k, r in b.readings.items()}
        assert {k: (u.count, u.ecu, u.samples) for k, u in a.unknown.items()} == \
            {k: (u.count, u.ecu, u.samples) for k, u in b.unknown.items()}
        # Latencies survive: frames keep their recorded spacing.
        lat_a = [m.latency_ms for m in a.messages if m.latency_ms is not None]
        lat_b = [m.latency_ms for m in b.messages if m.latency_ms is not None]
        assert len(lat_a) == len(lat_b) and all(
            abs(x - y) < 0.5 for x, y in zip(lat_a, lat_b))

        # A raw Save Log (no header, no epoch column) reads too.
        log = Path(d) / "log.csv"
        log.write_text("time_s,direction,can_id,dlc,data_hex,note\n"
                       "0.500,rx,7E8,8,05 62 F4 05 00 7B 00 00,\n")
        info, frames = read_capture(log)
        assert not info.is_demo and frames[0][1].data[5] == 0x7B
        bad = Path(d) / "bad.csv"
        bad.write_text("a,b\n1,2\n")
        try:
            read_capture(bad)
            raise AssertionError("a non-capture CSV was accepted")
        except ValueError:
            pass


def check_encode_field() -> None:
    p = bytes.fromhex("0308240004")
    for fld, raw in [(Field(2, 1), 0x7F), (Field(0, 2), 0xBEEF),
                     (Field(1, 2, signed=True), -1234),
                     (Field(0, 1, bit_shift=4, bit_len=4), 0xC),
                     (Field(0, 1, True, 0, 4), -3), (AUTO_FIELD, -2000)]:
        out = encode_field(p, fld, raw)
        assert decode_field(out, fld) == raw, (fld, raw, out.hex())
        assert len(out) == len(p)
    # Bytes and bits outside the field are kept.
    out = encode_field(p, Field(0, 1, bit_shift=4, bit_len=4), 0xF)
    assert out == bytes.fromhex("f308240004"), out.hex()
    # Clamped to the field; short payloads grow.
    assert encode_field(b"", Field(0, 1), 999) == b"\xff"
    assert encode_field(b"\x01", Field(2, 1), 5) == b"\x01\x00\x05"
    assert field_range(Field(0, 2, signed=True), 2) == (-32768, 32767)
    assert field_range(AUTO_FIELD, 1) == (0, 255)
    req = Request(0x20, 0x2B, -40.0, 0.1)
    assert req.raw_for(25.0) == 290 and abs(req.decode(
        encode_field(b"\x00\x00", AUTO_FIELD, req.raw_for(25.0))) - 25.0) < 1e-9


class _VirtualBus:
    """Delivers each node's frames to every other node, in order."""

    def __init__(self) -> None:
        from collections import deque
        self.queue = deque()
        self.nodes = []
        self.log = []

    def sender(self, index: int):
        return lambda frame: self.queue.append((index, frame))

    def run(self) -> None:
        while self.queue:
            src, frame = self.queue.popleft()
            self.log.append((src, frame))
            for i, node in enumerate(self.nodes):
                if i != src:
                    node.on_frame(frame)


def _recorded_car() -> list:
    """A made-up capture of a dongle talking to the brake module (713/77D):
    a session, a VIN read (multi-frame), and DID 18 10 read twice."""
    from .emulator import sdu_frames
    vin = b"WVWZZZ1KZAW000000"
    script = [
        (0.0, 0x713, bytes([0x10, 0x03])),
        (0.01, 0x77D, bytes([0x50, 0x03, 0x00, 0x32, 0x01, 0xF4])),
        (0.1, 0x713, bytes([0x22, 0xF1, 0x90])),
        (0.11, 0x77D, bytes([0x7F, 0x22, 0x78])),  # response pending
        (0.12, 0x77D, bytes([0x62, 0xF1, 0x90]) + vin),
        (1.0, 0x713, bytes([0x22, 0x18, 0x10])),
        (1.01, 0x77D, bytes([0x62, 0x18, 0x10, 0x00, 0x00, 0x01])),
        (5.0, 0x713, bytes([0x22, 0x18, 0x10])),
        (5.01, 0x77D, bytes([0x62, 0x18, 0x10, 0x0C, 0x9E, 0x01])),
    ]
    frames = []
    for t, can_id, sdu in script:
        for k, data in enumerate(sdu_frames(sdu)):
            frames.append((t + k * 0.001, CanFrame(t + k * 0.001, can_id, data)))
    # The dongle's flow control for the VIN.
    frames.insert(5, (0.1205, CanFrame(0.1205, 0x713, bytes([0x30, 0, 0, 0, 0, 0, 0, 0]))))
    return frames


def check_fake_car() -> None:
    from .emulator import FakeCar, Recording, SimDongle

    rec = Recording.from_frames(_recorded_car())
    assert rec.modules == {0x713: 0x77D}, rec.modules
    # Padding comes from the car's answers, however the dongle pads.
    dongle_padded = [(t, CanFrame(t, f.can_id, f.data[:-1] + b"\x00")
                      if f.can_id == 0x713 and f.data[0] >> 4 == 0 else f)
                     for t, f in _recorded_car()]
    dongle_padded += [(9.0 + i, CanFrame(9.0 + i, 0x713, bytes.fromhex("023e000000000000")))
                      for i in range(5)]
    assert Recording.from_frames(dongle_padded).pad == 0xAA
    assert rec.did_payload(0x713, (0x18, 0x10), at=2.0) == b"\x00\x00\x01"
    assert rec.did_payload(0x713, (0x18, 0x10)) == b"\x0c\x9e\x01"

    for block in (0, 1):
        bus = _VirtualBus()
        car = FakeCar(bus.sender(0))
        dongle = SimDongle(bus.sender(1), block_size=block)
        bus.nodes = [car, dongle]
        car.recording = rec

        def ask(tx: int, sdu: bytes):
            dongle.answers.clear()
            dongle.request(tx, sdu)
            bus.run()
            return dongle.answers

        # Multi-frame VIN, flow-controlled (block size 1: an FC per frame).
        got = ask(0x713, bytes([0x22, 0xF1, 0x90]))
        assert got == [(0x77D, bytes([0x62, 0xF1, 0x90]) + b"WVWZZZ1KZAW000000")], got
        # The handshake request replays as recorded.
        assert ask(0x713, b"\x10\x03")[0][1] == bytes([0x50, 0x03, 0x00, 0x32, 0x01, 0xF4])

    # Freeze: DID 18 10 as it was at 2 s, then at the end.
    car.freeze_at = 2.0
    assert ask(0x713, b"\x22\x18\x10") == [(0x77D, bytes.fromhex("621810000001"))]
    car.freeze_at = None
    assert ask(0x713, b"\x22\x18\x10")[0][1] == bytes.fromhex("6218100c9e01")
    # An override wins, for that DID only.
    car.set_override(0x713, 0x77D, (0x18, 0x10), b"\x05\x00\x01")
    assert ask(0x713, b"\x22\x18\x10")[0][1] == bytes.fromhex("621810050001")
    assert ask(0x713, b"\x22\xF1\x90")[0][1][:3] == b"\x62\xF1\x90"
    # Nothing known for this DID: out of range. Unknown module: silence.
    assert ask(0x713, b"\x22\x99\x99") == [(0x77D, b"\x7f\x22\x31")]
    assert ask(0x7E1, b"\x22\x18\x10") == []
    # Defaults: tester present (and its no-reply form), session control.
    assert ask(0x713, b"\x3e\x00") == [(0x77D, b"\x7e\x00")]
    assert ask(0x713, b"\x3e\x80") == []
    assert car.asked[(0x713, b"\x3e\x80")].how.startswith("tester present")
    # An override alone makes a module, with no recording at all.
    car.recording = None
    car.set_override(0x7E0, 0x7E8, (0xF4, 0x05), b"\x7b")
    assert ask(0x7E0, b"\x22\xF4\x05") == [(0x7E8, b"\x62\xf4\x05\x7b")]
    assert ask(0x7E0, b"\x10\x03")[0][1][:2] == b"\x50\x03"
    # A long request (two DIDs plus) is flow-controlled and answered.
    car.set_override(0x7E0, 0x7E8, (0xF4, 0x0C), b"\x0b\xb8")
    car.set_override(0x7E0, 0x7E8, (0xF4, 0x0D), b"\x32")
    car.set_override(0x7E0, 0x7E8, (0xF4, 0x11), b"\x10")
    got = ask(0x7E0, bytes.fromhex("22f405f40cf40df411"))
    assert got == [(0x7E8, bytes.fromhex("62f4057bf40c0bb8f40d32f41110"))], got
    # Every answer frame the car sent is marked as ours.
    assert all(f.direction == "tx" for src, f in bus.log if src == 0)


def check_background_frames() -> None:
    from .emulator import Recording

    frames = _recorded_car()
    heartbeat = bytes.fromhex("2010000000000000")
    # A heartbeat through the whole capture (two of them queued up at the
    # start), and a dongle probe that's over in a second.
    frames += [(0.0, CanFrame(0.0, 0x17F00010, heartbeat, extended=True))]
    frames += [(0.5 * i, CanFrame(0.5 * i, 0x17F00010, heartbeat, extended=True))
               for i in range(11)]
    frames += [(0.1 * i, CanFrame(0.1 * i, 0x200, bytes.fromhex("01c00010000301")))
               for i in range(10)]
    frames.sort(key=lambda tf: tf[0])
    bg = Recording.from_frames(frames).background
    assert [(b.can_id, b.extended, b.data) for b in bg] == [(0x17F00010, True, heartbeat)], bg
    assert abs(bg[0].period - 0.5) < 1e-6, bg[0].period


def check_capture_summary() -> None:
    from .emulator import Recording, summarize

    frames = _recorded_car()
    # An engine read with no session first, and one the engine refuses.
    for t, sdu, can_id in ((6.0, b"\x22\xf4\x05", 0x7E0), (6.01, b"\x62\xf4\x05\x7b", 0x7E8),
                           (7.0, b"\x22\x99\x99", 0x7E0), (7.01, b"\x7f\x22\x31", 0x7E8)):
        data = bytes([len(sdu)]) + sdu + b"\xaa" * (7 - len(sdu))
        frames.append((t, CanFrame(t, can_id, data)))
    text = "\n".join(summarize(Recording.from_frames(frames)))
    assert "Module 713 -> 77D" in text and "Module 7E0 -> 7E8" in text, text
    assert "10 03  session control" in text, text
    assert "read data (2 DIDs)" in text and "3x  3 ok" in text, text
    assert "1 ok, NRC 31 x1" in text, text
    assert text.splitlines()[-1].endswith("before its first read: 713"), text


def check_serial_reader_is_prompt() -> None:
    """A frame reaches on_frame within a few ms of arriving, not batched
    for 0.1 s: the fake car has 50 ms to answer the dongle."""
    import os
    import time
    from .sources import SlcanSerialSource

    master, slave = os.openpty()
    seen: list[float] = []
    src = SlcanSerialSource(os.ttyname(slave), lambda f: seen.append(time.monotonic()),
                            listen_only=False)
    src.start()
    try:
        os.write(master, b"\a")  # the standalone sniffer's SNIFFREQ reply
        deadline = time.monotonic() + 2.0
        while src.state != src.STREAMING and time.monotonic() < deadline:
            time.sleep(0.01)
        assert src.state == src.STREAMING, src.state
        time.sleep(0.05)
        worst = 0.0
        for _ in range(5):
            seen.clear()
            sent = time.monotonic()
            os.write(master, b"t7E080210030000000000\r")
            while not seen and time.monotonic() - sent < 0.5:
                time.sleep(0.0005)
            assert seen, "frame never arrived"
            worst = max(worst, seen[0] - sent)
            time.sleep(0.03)
        assert worst < 0.02, f"frames took up to {worst * 1000:.0f} ms to arrive"
    finally:
        src.stop()
        os.close(master)
        os.close(slave)


def check_sweep() -> None:
    from .emulator import Sweep

    sw = Sweep(0, 10, 4)
    assert [sw.advance() for _ in range(5)] == [0, 4, 8, 10, 10] and sw.done
    assert sw.advance(-1) == 6
    down = Sweep(100, 90, 5)
    assert [down.advance() for _ in range(4)] == [100, 95, 90, 90]


CHECKS = [
    check_auto_field_matches_legacy,
    check_explicit_fields,
    check_request_decode_and_ecu_match,
    check_units,
    check_enum_label,
    check_json_round_trip,
    check_degrees_alias,
    check_registry_loads,
    check_unknown_did_records_ecu,
    check_distinct_samples,
    check_fill_wip_ecus,
    check_engine_latest,
    check_simplest_field,
    check_capture_round_trip,
    check_encode_field,
    check_fake_car,
    check_background_frames,
    check_capture_summary,
    check_serial_reader_is_prompt,
    check_sweep,
    check_firmware_validation,
    check_internal_signals,
    check_fit_recovers_scale,
    check_fit_reports_what_it_cant_do,
    check_search_finds_packed_field,
    check_enum_mapping,
]


def run_checks() -> bool:
    ok = True
    for check in CHECKS:
        try:
            check()
            print(f"  ok    {check.__name__}")
        except AssertionError as e:
            ok = False
            print(f"  FAIL  {check.__name__}: {e}")
    return ok
