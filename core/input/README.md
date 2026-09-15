# core/input/

Physical buttons, from raw level to a named action. Three layers, so that **adding a gesture, adding an action, and deciding which triggers which are three separate changes** — and only the third is something a user does.

```
IButtons  →  ButtonGesture / ButtonInput  →  ActionMap  →  handlers
(raw level)     (what was performed)       (what it means)  (what it does)
```

Pure and clock-free — `now_ms` is passed in, the same way [`core::ISignalSource::tick()`](../vehicle/inc/signal_source.hpp) takes it. An entire press-hold-release sequence is therefore a host test with a loop variable, rather than something to verify with a finger and a stopwatch.

Not compiled into `env:teensymm`; that board has a touchscreen.

## The buttons are not an `lv_indev`

They drive app logic directly. No `lv_group`, no focus traversal, no LVGL input plumbing. That is the largest simplification a touchless design buys, and **it holds only as long as buttons never navigate.** Everything a button acknowledges is a transient overlay on `lv_layer_top()`, not a screen.

## Gesture vocabulary

| Event | When |
|---|---|
| `ShortPress` | Confirmed once the multi-click window closes |
| `DoublePress` | On the *second* release — there is no triple press to wait for |
| `LongPress` | **At** the threshold, still held |
| `HoldRepeat` | Repeats while held — a ramp, not a stepper |
| `LongRelease` | Letting go of a long hold |
| `VeryLongPress` | ~4 s, for things that should be hard to do by accident |

Defaults: 25 ms debounce, 300 ms multi-click gap, 700 ms long, 150 ms repeat, 4000 ms very long. They live in a `GestureTimings` struct rather than as bare constants, so a test can drive a compressed timeline and so they become editable later without touching the machine.

### The ordering guarantee

A single hold produces gestures in exactly this order, each one-shot at most once:

```
LongPress → HoldRepeat* → VeryLongPress → LongRelease
```

and a hold that goes long **never** also produces `ShortPress` or `DoublePress` on release. That is the classic bug in this kind of state machine, and it is why `LongRelease` exists as its own event rather than being inferred from a release.

`LongPress` *is* legitimately on the way to `VeryLongPress` — it already fired at its threshold and something has already happened on screen, so it is not retracted. **A source should therefore map either `HoldRepeat` (a continuous ramp) or `VeryLongPress` (a deliberate escalation), not two unrelated actions to both.** The intended shape for the escalating case is that `LongPress` starts something visible and `VeryLongPress` commits it — which is exactly how the chord's factory reset works.

## Chords, without a latency tax

Both buttons together is a natural "confirm" or "escape". The obvious implementation — wait to see whether a second button joins before acting on the first — taxes *every* single press with a chord window, on top of the multi-click window it already pays.

So a chord is recognised **only at the long-press threshold**: *the other button was already down when this one's `LongPress` came due, and neither has gone long yet during this hold.* There is no chord short press, so **a single short press costs exactly what it costs on a one-button board** — asserted directly in `test_a_short_press_is_not_delayed_by_having_a_second_button`.

The "neither has gone long yet" half is what separates the two cases worth distinguishing:

- **Press both together** — the first to reach its threshold finds the other down and unfired: **one** chord, and both buttons suppressed for the rest of the hold.
- **Press one, hold it past the threshold, then press the other** — the first has already fired its own `LongPress`, so no chord forms: **two** independent long presses, no chord.

Suppression covers *everything* for the remainder of the hold, not only `LongPress`. This goes slightly beyond what the build plan specified, for a concrete reason: suppressing only the long press leaves letting go of a chord reading as two short presses, which is a worse bug than the one suppression exists to fix.

## `ActionMap` — remapping needs no firmware change

A flat array of three-byte `ActionBinding{source, event, action}` records, sized to go straight into the persisted config and be edited from the web form — exactly like [`core::SensorCal`](../sensors/README.md). Adding an *action* is one enum value plus one handler; adding a *binding* is a form entry.

Default two-button map (defaults only — nothing here is a commitment once it is editable):

| Source | Gesture | Action |
|---|---|---|
| A | `ShortPress` | Cycle zoom |
| A | `DoublePress` | Day / night brightness |
| A | `LongPress` + `HoldRepeat` | Ramp brightness |
| B | `ShortPress` | Reset peak hold |
| B | `DoublePress` | Metric / imperial |
| B | `LongPress` | Wi-Fi on / off |
| Chord | `LongPress` | Factory reset (arm) |
| Chord | `VeryLongPress` | Factory reset (commit) |

**A latched warning pre-empts everything.** `resolve(source, event, warning_latched)` returns `AcknowledgeWarning` for any *discrete* press on any source — a driver reaching for the dashboard because something lit up should not have to remember which button dismisses it. `HoldRepeat` and `LongRelease` are deliberately excluded; acknowledging once every 150 ms is not a gesture.

### What one button cannot reach

The build plan said a single-button board moves "everything on B" to A's `VeryLongPress`. That does not fit: B carries three actions and `VeryLongPress` is one slot. One button has four performable gestures against six-plus actions, so `set_defaults(1)` **re-shapes** rather than truncates — the ramp subsumes the day/night toggle, and Wi-Fi moves to the gesture nobody hits by accident.

Peak-hold reset and factory reset are then **not reachable at all**. That is asserted in `test_single_button_fallback_reshapes_and_cannot_reach_everything` rather than left implicit, so nobody later believes the plan's "still reaches every action" of this configuration.

## Files

- **`inc/button_gesture.hpp`** / **`src/button_gesture.cpp`** — `ButtonGesture`, one instance per button. Level debouncing, then the gesture machine on the debounced level. Knows nothing about other buttons; the chord recogniser reaches in through `suppress_until_release()`.
- **`inc/button_input.hpp`** / **`src/button_input.cpp`** — `ButtonInput`: owns one gesture per button, runs the chord recogniser, and presents the lot as a short queue of `InputEvent{source, event}` the composition root drains. `INPUT_SOURCE_CHORD` is deliberately not a button index, so a binding cannot collide with a real button.
- **`inc/action_map.hpp`** / **`src/action_map.cpp`** — `InputAction`, `ActionMap`, and `input_action_name()` for the web form's gesture editor.

## Dependency shape

```
core/input → platform/platform.hpp (IButtons)
```

That is all. It does not know the catalog, the store, the config or `gui/` exist.

## Tests

[`test/test_core_input/`](../../test/README.md) — 19 cases over a synthetic timeline, with expected instants computed from `GestureTimings` rather than written as literals, so moving a threshold moves the tests with it instead of breaking them.

## Persisting a map

`binding_count()`/`binding_at()` write it out; `load(bindings, count)` reads it back. [core/device_config](../device_config/README.md) stores the table verbatim, so a remap survives a power cycle without anything in between understanding what a binding means.

`load()` goes through `bind()` rather than memcpy-ing the table, because **a saved blob is data, not a caller that can be trusted to have been well-formed**: a duplicate `(source, event)` collapses instead of occupying two slots that then disagree, and a count past `MAX_BINDINGS` is dropped instead of overrunning the array. It also *replaces* rather than merges — leaving old bindings underneath would resurrect a gesture the owner had deliberately removed.

The marshalling between an `ActionMap` and the persisted struct lives in the composition root, not on either type: `ActionMap` should not know it is ever saved, and `DeviceConfig` is plain data.
