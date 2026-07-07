# core/

Platform-agnostic vehicle domain logic: scheduling, the UDS protocol, the signal catalog/runtime store, and the persisted config schema. Nothing in `core/` includes LVGL, Arduino, FlexCAN_T4, or SDL — the only outside dependency allowed is `platform/platform.hpp`'s interfaces (`ICanBus`, `IConfigStore`, `IClock`), never a concrete `teensy_*`/`simulator_*` class. This is what makes `core/` host-buildable and unit-testable (see [test/](../test/README.md)) without any hardware or a display.

Every collection in `core/` is a fixed-size array sized by a `constexpr` count known at compile time, and every callback is a C-style function pointer plus a `void *ctx` — no heap allocation, matching the project's RAM-constrained-embedded-target constraint.

## Subfolders

- **[time/](time/README.md)** — `core::Scheduler`, a periodic task runner. Drives CAN request pacing and the Info screen's refresh cadence.
- **[can/](can/README.md)** — `core::UdsClient`, the UDS request/response protocol over `ICanBus`.
- **[vehicle/](vehicle/README.md)** — the signal catalog (`SignalSpec`, `SIGNAL_CATALOG`), the runtime value store (`SignalStore`), and warning-threshold evaluation.
- **[config/](config/README.md)** — the persisted config schema (`Config`, `Settings`, `PanelConfig`, `SignalConfig`) and `ConfigStore`'s load/save/migrate logic over `IConfigStore`.

## Dependency shape

```
core/time    → platform/platform.hpp (IClock)
core/can     → platform/platform.hpp (ICanBus)
core/vehicle → core/can (UdsRequestSpec)
core/config  → core/vehicle (SIGNAL_CATALOG_COUNT, WarningThresholds), platform/platform.hpp (IConfigStore)
```

Nothing in `core/` depends on `gui/`. The only code allowed to depend on both `core/` and `gui/` is [gui/bindings](../gui/bindings/README.md).
