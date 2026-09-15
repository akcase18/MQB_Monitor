/**
 * @file device_config.hpp
 * @brief What the hardware *is*, as opposed to what the dashboard *shows*.
 *
 * core/config's `Config` describes presentation -- which signals are on
 * which panels, in which units, with which warning thresholds. It is
 * shared by every product in this tree and its shape is dictated by the
 * signal catalog.
 *
 * This is the other half, and only a product with sensors and buttons has
 * one: which sensor is wired to which ADC channel and how to convert it
 * (core::SensorCal), and what each button gesture does
 * (core::ActionBinding). Nothing here is about signals; a channel names a
 * catalog id, but the numbers around it describe a part somebody bought.
 *
 * ## Why a separate blob rather than fields on `Config`
 *
 * Measured during Phase 6.0: a gauge product `Config` is **164 bytes**,
 * and `SensorCal[SENSOR_MAX_CHANNELS]` alone is **1,760**. Folding them
 * together would make calibration ten times the size of everything else in
 * the shared schema, and would make the CAN product carry all of it for
 * hardware it does not have. Two blobs, two magics, two versions: each
 * product persists what it actually has, and a schema change to one cannot
 * force a migration of the other.
 *
 * ## The header is self-describing on purpose
 *
 * `magic` alone says "some firmware wrote this". It does not say the
 * firmware agreed about the *shape*. So the header also carries
 * `struct_size` and `catalog_count`, and `DeviceConfigStore::load()`
 * rejects a mismatch exactly the way it rejects a bad magic.
 *
 * That is not theoretical carefulness. This tree has already had one
 * product load another's config blob and silently blank a gauge, because
 * both wrote the same magic and version at the same offset and differed
 * only in size. A `signal_id` inside a `SensorCal` is an index into a
 * catalog that is generated per product and can grow; `catalog_count`
 * is the cheapest fingerprint that notices when it has.
 */
#pragma once

#include <stdint.h>

#include "action_map.hpp"
#include "sensor_cal.hpp"
#include "signal_catalog.hpp"

namespace core
{

    /** Distinct from core::ConfigStore::MAGIC_NUMBER (0xFEA7). The two
     * blobs live in the same backing store at different offsets, and
     * neither may ever validate against the other. */
    constexpr uint16_t DEVICE_CONFIG_MAGIC = 0x6A17;

    constexpr uint8_t DEVICE_CONFIG_VERSION = 1;

    static_assert(SIGNAL_CATALOG_COUNT <= 255,
                  "catalog_count is a uint8_t fingerprint");

    struct DeviceConfigHeader
    {
            uint16_t magic;
            uint8_t  version;

            /** core::SIGNAL_CATALOG_COUNT as it was when this was written.
             * A saved channel names a catalog id; if the catalog has
             * changed size underneath it, those ids are no longer
             * trustworthy. */
            uint8_t catalog_count;

            /** sizeof(DeviceConfig) as it was when this was written. */
            uint16_t struct_size;

            /** How many of `sensors[]` describe a real channel. */
            uint8_t sensor_channels;

            /** How many of `bindings[]` are in use. */
            uint8_t binding_count;
    };

    static_assert(sizeof(DeviceConfigHeader) == 8,
                  "DeviceConfigHeader is written to storage byte-for-byte");

    struct DeviceConfig
    {
            DeviceConfigHeader header;

            SensorCal sensors[SENSOR_MAX_CHANNELS];

            ActionBinding bindings[ActionMap::MAX_BINDINGS];

            /**
             * How long a warning threshold crossing must persist before it
             * raises anything -- core::WarningLatch's qualify time.
             *
             * Configuration rather than a constant because it is the knob
             * that resolves the oil-pressure threshold problem: with no
             * RPM input, whether a dip at idle is noise or a fault is a
             * judgement about a particular engine, and the owner is the
             * one who can make it.
             */
            uint16_t warning_qualify_ms;

            /**
             * core::AnalogSignalSource's round-robin slot interval, ms. A
             * channel's own sample period is this times the number of
             * configured channels.
             */
            uint32_t sample_interval_ms;
    };

} // namespace core
