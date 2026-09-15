# core/device_config/

What the hardware *is*, as opposed to what the dashboard *shows*.

[core/config](../config/README.md)'s `Config` describes presentation — which signals sit on which panels, in which units, with which warning thresholds. Every product in this tree has one, and its shape is dictated by the signal catalog.

This is the other half, and only a product with sensors and buttons has one:

- **`SensorCal sensors[SENSOR_MAX_CHANNELS]`** — which sensor is wired to which ADC channel and how to convert it ([core/sensors](../sensors/README.md)).
- **`ActionBinding bindings[MAX_BINDINGS]`** — what each button gesture does ([core/input](../input/README.md)).
- **`warning_qualify_ms`** — how long a threshold crossing must persist before it raises anything ([`WarningLatch`](../vehicle/README.md)). Configuration rather than a constant because with no RPM input, whether a dip at idle is noise or a fault is a judgement about a particular engine.
- **`sample_interval_ms`** — `AnalogSignalSource`'s round-robin slot period.

Not built into `env:teensymm` — that board has neither sensors nor buttons.

## Why a separate blob rather than fields on `Config`

Measured during Phase 6.0: a gauge-product `Config` is **164 bytes**, and `SensorCal[SENSOR_MAX_CHANNELS]` alone is **1,760**. Folding them together would make calibration ten times the size of everything else in the shared schema, and would make the CAN product carry all of it for hardware it does not have.

Two blobs, two magics, two versions. Each product persists what it actually has, and a schema change to one cannot force a migration of the other. They share one `IConfigStore` at different offsets:

```
offset 0                             core::Config          (164 B gauge, 620 B CAN)
offset core::DEVICE_CONFIG_OFFSET    core::DeviceConfig    (1,824 B)
        = 1024
```

`DEVICE_CONFIG_OFFSET` is a **fixed address with headroom, not `sizeof(Config)`**. Deriving it would move the blob every time the shared schema grew by a field, silently orphaning a calibration somebody entered by hand — the magic would simply not be where it used to be, and the store would report a fresh device. The composition root carries a `static_assert` that `Config` has not grown into it; it is the only file that sees both schemas.

## The header is self-describing on purpose

```cpp
struct DeviceConfigHeader
{
        uint16_t magic;           // 0x6A17, distinct from Config's 0xFEA7
        uint8_t  version;
        uint8_t  catalog_count;   // SIGNAL_CATALOG_COUNT when written
        uint16_t struct_size;     // sizeof(DeviceConfig) when written
        uint8_t  sensor_channels;
        uint8_t  binding_count;
};
```

`magic` alone says "some firmware wrote this". It does not say the firmware agreed about the *shape*. That distinction is not theoretical here: this tree has already had one product load another's blob and silently blank a gauge, because both wrote the same magic and version at the same offset and differed only in size (see [platform/simulator](../../platform/simulator/README.md)). And a `signal_id` inside a `SensorCal` indexes a catalog that is generated per product and can grow — `catalog_count` is the cheapest fingerprint that notices when it has.

`load()` rejects on any disagreement and discards the blob **whole**. A half-trusted calibration is a gauge reading a plausible, confident, wrong number, which is the single failure the entire conditioning layer exists to prevent.

## It has no defaults of its own

`load()` reports whether it found something usable and stops. It does not substitute a factory calibration, because there is no such thing at this layer: which sensor is on channel 0, and what its full scale is, are facts about a particular board and belong to the composition root that knows them — the same way `src/main.cpp` owns its CAN arbitration IDs.

That is Phase 6.0's lesson applied ahead of time. `ConfigStore::set_defaults()` had six MQB catalog ids baked into shared code and was silently wrong for every other product. Here, shared code holds the container and the validity rules; the root holds the numbers.

Three return states, and the caller needs all three:

| | Meaning |
|---|---|
| `load()` true | A usable configuration was read. |
| `load()` false, `last_load_rejected()` false | Erased storage — an ordinary first boot. |
| `load()` false, `last_load_rejected()` true | A blob of ours that disagreed about its shape. **Somebody's calibration was just discarded.** |

Plus `storage_too_small()`, checked up front because the failure is otherwise invisible and baffling: every write fails, every load finds nothing, and the device re-seeds factory defaults on every single boot while appearing to accept changes. Bringing up a new board's `IConfigStore` is exactly where that happens, so `src/main_gauge.cpp` says it out loud on screen.

## Files

- **`inc/device_config.hpp`** — the schema. Flat POD, written byte-for-byte.
- **`inc/device_config_store.hpp`** / **`src/device_config_store.cpp`** — `DeviceConfigStore`: `load()`, `save()`, and the partial writers `save_sensor(index)` / `save_bindings()` for a web form's per-channel Save, which should not rewrite eighteen hundred bytes to change four.

## Usage

`src/main_gauge.cpp` owns the one instance. On a first boot (or a rejection) it fills the blob with this board's factory configuration and saves; otherwise it loads and pushes the result into `AnalogSignalSource::configure()`, `ActionMap::load()` and `WarningLatch::set_qualify_ms()`.

Marshalling between `ActionMap` and the blob lives in the root, not on either type: `ActionMap` should not know it is ever persisted, and `DeviceConfig` is plain data.
