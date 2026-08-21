# test/

Host-buildable Unity test suites for the logic in [core/](../core/README.md) and [gui/screens/layout.hpp](../gui/screens/README.md) — no hardware, no LVGL rendering. These build and run entirely on the host machine because `core/` and `gui/screens/layout.cpp` have no Arduino/SDL/LVGL-rendering dependency.

```sh
pio test -e simulator
```

Running tests overwrites the simulator environment's build output — run `pio run -e simulator` afterward to restore the interactive simulator binary before using it again.

## Suites

- **`test_core_config/`** — [core/config](../core/config/README.md)'s `ConfigStore` and [core/vehicle](../core/vehicle/README.md)'s `evaluate_warning()`: defaults on blank storage, save/load round-trips (including `display_mode` and partial `save_priority`/`save_signal` writes), and falling back to defaults on an unrecognized config version.
- **`test_core_vehicle/`** — [core/vehicle](../core/vehicle/README.md)'s `SignalStore` and [core/time](../core/time/README.md)'s `Scheduler`: single- and multi-request signal round-trips (a multi-request signal doesn't resolve until *all* its requests report), unit-conversion/display-value correctness, that every catalog entry's `display_min < display_max`, metric↔imperial round-tripping, and that the scheduler actually fires tasks once they're due.
- **`test_gui_layout/`** — [gui/screens](../gui/screens/README.md)'s `compute_layout()` and `compute_prominence_ranks()`: the exact shape of each panel count (1 full-screen, 2 equal columns, 3 hero+stack, 4 even 2×2, 5 matching the legacy fixed layout, 6 even 2×3), that every count's slots fully tile the grid with no gaps/overlaps, that vertical orientation is a true transpose of horizontal (including prominence ranks) for every count except 5 — which gets its own bespoke Vertical shape (a 2×2 of small panels over one wider, higher-priority panel), covered by its own dedicated tests instead — that an out-of-range count gets clamped to 1–6, and that priority rank 0 always resolves to the most prominent slot (hero > large > small) regardless of raw slot index.

## Adding a test

Follow an existing suite's shape: a `test_main.cpp` with one `void test_*()` per case, `RUN_TEST()`'d from `main()`/`setup()`. A new suite needs its own subdirectory under `test/` (PlatformIO's Unity runner discovers one binary per subdirectory) — see the two existing `core/` suites for how to construct fakes for `IConfigStore`/`IClock`/`ICanBus` without any real platform.
