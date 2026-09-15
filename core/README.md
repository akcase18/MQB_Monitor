# core/

Platform-agnostic vehicle domain logic: scheduling, the UDS protocol, the signal catalog/runtime store, and the persisted config schema. Nothing in `core/` includes LVGL, Arduino, FlexCAN_T4, or SDL — the only outside dependency allowed is `platform/platform.hpp`'s interfaces (`ICanBus`, `IConfigStore`, `IClock`), never a concrete `teensy_*`/`simulator_*` class. This is what makes `core/` host-buildable and unit-testable (see [test/](../test/README.md)) without any hardware or a display.

Every collection in `core/` is a fixed-size array sized by a `constexpr` count known at compile time, and every callback is a C-style function pointer plus a `void *ctx` — no heap allocation, matching the project's RAM-constrained-embedded-target constraint.

## Subfolders

- **[time/](time/README.md)** — `core::Scheduler`, a periodic task runner. Drives CAN request pacing and the Info screen's refresh cadence.
- **[can/](can/README.md)** — `core::UdsClient`, the UDS request/response protocol over `ICanBus`.
- **[vehicle/](vehicle/README.md)** — the signal catalog (`SignalSpec`, `SIGNAL_CATALOG`), the runtime value store (`SignalStore`), and warning-threshold evaluation.
- **[input/](input/README.md)** — `ButtonGesture`, `ButtonInput` and `ActionMap`: physical buttons from raw level to a named action, including the chord between them. Pure and clock-free; not compiled into `env:teensymm`.
- **[sensors/](sensors/README.md)** — `AnalogSignalSource` and the conditioning maths behind it: transfer functions, thermistor curve models, filtering, and the plausibility check that stops a broken wire from being displayed as a number. The analog counterpart to `can/`; not compiled into `env:teensymm`.
- **[config/](config/README.md)** — the persisted config schema (`Config`, `Settings`, `PrioritizedSignal`, `SignalConfig`) and `ConfigStore`'s load/save/migrate logic over `IConfigStore`.
- **[device_config/](device_config/README.md)** — the other half of persistence, and only a product with sensors and buttons has one: `SensorCal[]`, the action map, and the warning qualify time. `config/` describes what the dashboard *shows*; this describes what the hardware *is*. Its own blob at its own offset, because `SensorCal[8]` alone is ten times a gauge `Config` and describes nothing the CAN product has. Not compiled into `env:teensymm`.
- **[logging/](logging/README.md)** — `Logger`, the continuous on-change recorder over `IStorage` for whichever signals are currently on screen, and the on-disk `LogRecord`/`ChunkHeader` formats its rotating chunk files use.

## Dependency shape

```
core/time    → platform/platform.hpp (IClock)
core/can     → core/vehicle (SignalStore, ISignalSource), platform/platform.hpp (ICanBus)
core/sensors → core/vehicle (SignalStore, ISignalSource), platform/platform.hpp (IAnalogInput)
core/input   → platform/platform.hpp (IButtons) -- and nothing else at all
core/vehicle → nothing in core/ (it owns what a signal *is*; acquisition depends on it, never the reverse)
core/config  → core/vehicle (SIGNAL_CATALOG_COUNT, WarningThresholds), platform/platform.hpp (IConfigStore)
core/logging → core/vehicle (SIGNAL_CATALOG, SignalStore), core/config (Config — which signals are on screen, so which get logged), platform/platform.hpp (IStorage)
```

Nothing in `core/` depends on `gui/`. The only code allowed to depend on both `core/` and `gui/` is [gui/bindings](../gui/bindings/README.md).
