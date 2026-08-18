# tools/

Developer tooling for the MQB dashboard project — host-side programs that
support firmware development but don't ship on any board.

## Contents

- **[`gen_catalog.py`](gen_catalog.py)** — compiles the firmware's signal
  catalog from the registry. The single source of truth is
  [`core/vehicle/data/signals.json`](../core/vehicle/data/signals.json) — a
  **two-stage registry**: every signal ever defined lives there, tagged
  `"status": "active"` or `"status": "wip"` (unconfirmed offset/scale, a
  placeholder name, etc.). This script filters to `status == "active"` only,
  emitting `core/vehicle/src/signal_catalog.generated.inc` (the
  `SIGNAL_CATALOG[]` array `signal_catalog.cpp` includes) **and** keeping
  `SIGNAL_CATALOG_COUNT` in `signal_catalog.hpp` in sync with the active
  count. WIP entries never reach the firmware. Run it after editing
  `signals.json`; commit the JSON and the generated file together.

  ```sh
  python3 tools/gen_catalog.py          # regenerate
  python3 tools/gen_catalog.py --check  # CI guard: fail if the .inc/count are stale
  ```

- **[`monitor/`](monitor/)** — the **MQB Monitor**, a Mac (PySide6) app that
  passively watches UDS traffic between an OBDEleven and the car via a Teensy
  4.0 CAN sniffer, decodes it against the *same* `signals.json` registry the
  firmware uses, and is how you add, review, and promote/demote signals — its
  Registry/Learn/WIP tabs read and write the registry directly. See its
  [README](monitor/README.md).

## The two-stage registry loop

```
                 core/vehicle/data/signals.json   ← every signal, active + WIP
                    │                        │
      gen_catalog.py│                        │Catalog.load()
      (active only) │                        │
                    ▼                        ▼
   signal_catalog.generated.inc        tools/monitor (Mac app)
   + SIGNAL_CATALOG_COUNT (hpp)         · decodes live traffic (active only)
                    │                    · Learn: discover unknown DIDs
   firmware build ◄─┘                    · Registry/WIP: review, promote/
                    ▲                        demote, edit -- writes back to
                    │                        signals.json
                    └──────────────  re-run gen_catalog.py  ◄─────────────┘
                       (or the Registry tab's Regenerate button)
```

The monitor's decode logic (`mqb_monitor/catalog.py`) mirrors the firmware's
(`core/can` + `core/vehicle`) byte-for-byte — same 1-byte-unsigned /
2-byte-signed payload rule, same `(raw + offset) * scale`, same transforms —
so a value shown in the tool matches what the dashboard would show. The same
mirroring applies to the active/WIP split: `Catalog.known_dids()` /
`signals_for_did()` only ever resolve **active** signals, so the live monitor
treats a WIP signal's DID as "unknown, log it raw" exactly like the firmware
would if it saw that DID — WIP status is a monitor-tool-only concept until
promoted.
