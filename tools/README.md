# tools/

Developer tooling for the MQB dashboard project — host-side programs that
support firmware development but don't ship on any board.

## Contents

- **[`gen_catalog.py`](gen_catalog.py)** — regenerates the firmware's signal
  catalog from the canonical data file. The single source of truth is
  [`core/vehicle/data/signals.json`](../core/vehicle/data/signals.json); this
  script emits `core/vehicle/src/signal_catalog.generated.inc` (the
  `SIGNAL_CATALOG[]` array `signal_catalog.cpp` includes) **and** keeps
  `SIGNAL_CATALOG_COUNT` in `signal_catalog.hpp` in sync with the entry count.
  Run it after editing `signals.json`; commit the JSON and the generated file
  together.

  ```sh
  python3 tools/gen_catalog.py          # regenerate
  python3 tools/gen_catalog.py --check  # CI guard: fail if the .inc/count are stale
  ```

- **[`monitor/`](monitor/)** — the **MQB Monitor**, a Mac (PySide6) app that
  passively watches UDS traffic between an OBDEleven and the car via a Teensy
  4.0 CAN sniffer, decodes it against the *same* `signals.json` the firmware
  uses, and helps discover/label new UDS DIDs. See its
  [README](monitor/README.md).

## The shared-catalog loop

```
                 core/vehicle/data/signals.json   ← single source of truth
                    │                        │
      gen_catalog.py│                        │Catalog.load()
                    ▼                        ▼
   signal_catalog.generated.inc        tools/monitor (Mac app)
   + SIGNAL_CATALOG_COUNT (hpp)         · decodes live traffic
                    │                    · "learn mode" appends new
   firmware build ◄─┘                      signals back to signals.json ──┐
                    ▲                                                      │
                    └──────────────  re-run gen_catalog.py  ◄─────────────┘
```

The monitor's decode logic (`mqb_monitor/catalog.py`) mirrors the firmware's
(`core/can` + `core/vehicle`) byte-for-byte — same 1-byte-unsigned /
2-byte-signed payload rule, same `(raw + offset) * scale`, same transforms —
so a value shown in the tool matches what the dashboard would show.
