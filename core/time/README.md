# core/time/

`core::Scheduler` — a fixed-capacity (`MAX_TASKS = 32`), no-heap periodic task runner. It replaces what used to be LVGL timers (`lv_timer_create`) for pacing non-GUI work, so CAN polling cadence doesn't depend on the GUI framework's event loop.

## Files

- **`inc/scheduler.hpp`** / **`src/scheduler.cpp`** — `Scheduler`:
  - `every(period_ms, cb, ctx)` registers a task to run every `period_ms`, starting one period from now. Returns a `TaskId`, or `INVALID_TASK` if the fixed-size task table is full.
  - `tick()` runs any due tasks — called once per main-loop iteration from `src/main.cpp`'s `loop()`.

## Usage

`src/main.cpp` registers one `Scheduler` task per catalog entry in `core::SIGNAL_CATALOG` (each at that signal's own `cycle_time_ms`, sending its UDS requests) plus one task that refreshes the Info screen's panels — see `setup()` in [src/main.cpp](../../src/main.cpp). The scheduler itself has no idea what a "signal" or a "panel" is; it just calls function pointers on a schedule, driven by whatever `IClock` the current platform provides.
