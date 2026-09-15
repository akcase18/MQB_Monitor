# core/sensors/

Everything between an ADC pin and a number a driver will believe. The analog counterpart to [core/can](../can/README.md): where that module knows values arrive as UDS responses, this one knows they arrive as volts.

Pure maths and fixed-size arrays, no LVGL, no Arduino, no clock of its own — so all of it is host-testable against known inputs, which is the point. A wrong number on an oil pressure gauge comes from exactly this arithmetic.

## Why plausibility is the headline feature

With UDS, a dead sensor produced no response, `has_data` stayed false on its own, and the panel blanked without anyone writing code for it.

Analog has no such courtesy. **A broken wire produces a confident number.** An open thermistor leg leaves the divider node at the pull-up rail, which converts cleanly to about −86 °C. Nothing errors, nothing times out, no status bit is set. An oil pressure gauge reading a steady zero because a connector vibrated loose is worse than one reading nothing, because the driver believes it.

So every conversion returns a `SensorStatus` alongside its value, and `AnalogSignalSource` writes to the store **only** on `Ok`. Not a zero, not a stale value, not a flag for someone downstream to remember to check — nothing. `has_data` stays false, the panel blanks and the warning is suppressed, all through machinery that already existed. That is why `SignalStore::set_at()` was left as the only write path.

The band a reading is checked against is **derived from that channel's own `SensorCal`**, never from a constant. Fitting a different sender moves the fault window with it, automatically and with no firmware change.

## Files

- **`inc/sensor_cal.hpp`** — `SensorCal`: what is wired to one channel, as plain data. The divider in front of it, its transfer function, its filter constant, its plausibility margin. Flat POD, no pointers, no heap: it can be `memcpy`'d into the persisted config blob and filled field-by-field from an HTTP form with no marshalling in between. `SensorKind` is one of `Unconfigured`, `LinearVoltage`, `Thermistor`, `RailSense`.

- **`inc/conditioning.hpp`** / **`src/conditioning.cpp`** — the maths, as pure functions. `divider_source_volts()`, `linear_apply()`, `ntc_resistance()`, the three curve models, and `sensor_convert()` which chains them and returns the verdict. Also `sensor_cal_span()` — see [Ranges](#ranges-derive-from-the-sensor) below.

- **`inc/sensor_filter.hpp`** / **`src/sensor_filter.cpp`** — `SensorFilter`: median-of-three, then an EMA. Two stages because they defend against different things. The median discards single-sample outliers outright (an ignition event coupling into the harness), which an EMA would instead smear across the next several seconds; the EMA then does the actual smoothing. **The time constant is in seconds and the update is dt-aware**, not an alpha-per-sample — see the round-robin note below for why that matters.

- **`inc/curve_library.hpp`** / **`src/curve_library.cpp`** — built-in starting points. See [Presets](#presets-fill-a-calibration-in-they-are-not-a-mode).

- **`inc/analog_signal_source.hpp`** / **`src/analog_signal_source.cpp`** — `AnalogSignalSource`, the [`core::ISignalSource`](../vehicle/inc/signal_source.hpp) implementation. Deliberately the smallest file here; everything difficult is pure maths next door.

## Presets fill a calibration in; they are not a mode

`sensor_preset_apply()` **copies numbers into a `SensorCal` and is then finished with.** No conversion anywhere reads a preset id, and nothing needs the preset to still exist afterwards. Two consequences, and they are the reason it is built this way:

- **"Pick your sensor from a list" and "enter your own curve" are the same code path.** A combination — pick the closest preset, then correct one field against your datasheet — works without being designed for separately, and is not a second-class mode. The web form needs one editor, not two.
- **A firmware update cannot change what an already-configured gauge reads.** The numbers live in the saved config, not in whichever firmware happens to be installed. Adding, correcting or removing a preset is safe by construction.

`SensorCal::preset_id` survives as *provenance only*, so the form can say "Generic NTC 10k (edited)" rather than "Custom". Nothing computes with it, and an unrecognised id degrades to "Custom" rather than to a wrong curve.

**On vendor curves.** This table deliberately ships no Bosch/VDO/Delphi coefficients. Those parts are not interchangeable and a curve that is merely close is wrong by tens of degrees at the ends, so a number that has not been read off the actual datasheet has no business between a temperature sender and a warning light. The route for a specific part is `CurveModel::Lut` with the published R/T table, which needs no firmware change at all. Adding a *verified* curve here later is one row in the table.

## The three curve models

All three are supported because they cost almost nothing together and span the whole range from "I have two numbers" to "I have the datasheet's table".

| Model | Data needed | Good for |
|---|---|---|
| `Beta` | `R25`, `β` | Generic thermistors; running the same afternoon |
| `SteinhartHart` | `A`, `B`, `C` | Wide span, when three known points are available |
| `Lut` | up to 16 (R, T) points | Matching a published table exactly |

The LUT interpolates in **(ln R, 1/T) space**, not in straight lines between raw points — that is the space an NTC is actually linear in, and the Beta model is exactly a straight line there. So a table sampled from a curve reproduces that curve *between* its points instead of chording across it, and sixteen points comfortably covers −40…150 °C. Past either end it **extrapolates rather than clamping**: clamping would manufacture a plausible reading out of a sensor that has left its calibrated range, which is the exact failure this module exists to prevent.

## Ranges derive from the sensor

`sensor_cal_span()` reports the span a calibration claims to measure, and **two things read it**: the plausibility band widens it by `band_margin`, and (in Phase 7) saving a calibration seeds the gauge's display range from it. Their sharing one derivation is the point — swapping to a different sensor moves *what the bar spans* and *what counts as a fault* together, with nothing for the user to keep in sync by hand.

## Sampling is round-robin, one channel per slot

`tick()` runs in the same cooperative loop as `lv_timer_handler()`, so it does at most one I2C conversion per call rather than sweeping every channel and handing back a dropped frame. A channel's own sample interval is therefore `sample_interval_ms × configured_channels`.

That is why `SensorFilter` takes a `dt` rather than a fixed per-sample alpha: **adding a fourth sensor must not silently slow the other three down.**

Only channels feeding a displayed panel are sampled, mirroring [`SignalPoller`](../can/README.md)'s demand-driven behaviour — with one exception. A `RailSense` channel is never "displayed", but it is sampled whenever anything ratiometric is, because three good sensors read wrong together when the supply sags and nothing corrects for it.

## Ratiometric correction

Automotive pressure transducers output a fraction *of their own supply*, not an absolute voltage. A 5 V rail sagging to 4.8 V under load moves an uncorrected reading by 4%. When `LinearCal::ratiometric` is set, the measurement is referred back to `nominal_supply` using whichever channel is a `SensorKind::RailSense`.

This happens **before** the band check, or a sagging rail would read as a sensor drifting out of range. And an implausible rail reading is treated as *no* rail reading — correcting against a bad measurement would move every ratiometric sensor at once, so the code falls back to the configured nominal instead.

## Dependency shape

```
core/sensors → core/vehicle (SignalStore, ISignalSource, SIGNAL_CATALOG)
             → platform/platform.hpp (IAnalogInput)
```

Nothing depends on `core/sensors` except a composition root. It is not compiled into `env:teensymm` at all.

## Tests

[`test/test_core_sensors/`](../../test/README.md) — and note the second kind of test in there. The simulator's sensor model is written **backwards**: waveforms scripted in psi and °C, then pushed through the *inverse* of each transfer function to produce ADC volts. So running this module forwards over those volts has to land back on the number the simulator started from. A simulator emitting convenient voltages directly would let a wrong transfer function pass.

## `SENSOR_MAX_CHANNELS`

Eight — two ADS1115s' worth, since the second is an address-strap change rather than a board respin. It lives in `sensor_cal.hpp`, next to the struct it sizes, because it is a property of the *calibration data*: [core/device_config](../device_config/README.md) persists that many `SensorCal`s, so raising it later is a schema migration rather than a constant edit. `AnalogSignalSource::MAX_CHANNELS` is taken from it rather than declared again, so a source cannot accept a channel table it could never save.
