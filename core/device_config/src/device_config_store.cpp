#include "device_config_store.hpp"

#include <stddef.h>
#include <string.h>

#include "mem_placement.hpp"

namespace core
{

    namespace
    {
        size_t sensor_offset(uint8_t index)
        {
            return offsetof(DeviceConfig, sensors)
                   + static_cast<size_t>(index) * sizeof(SensorCal);
        }
    } // namespace

    DeviceConfigStore::DeviceConfigStore(IConfigStore &backing, uint32_t offset)
        : backing_(backing)
        , offset_(offset)
    {
        clear();
    }

    DeviceConfig &DeviceConfigStore::get()
    {
        return config_;
    }

    const DeviceConfig &DeviceConfigStore::get() const
    {
        return config_;
    }

    void DeviceConfigStore::clear()
    {
        /**
         * memset rather than value-initialisation: SensorCal is a flat POD
         * specifically so it can be written to storage byte-for-byte, and
         * zeroing it gives SensorKind::Unconfigured (0) on every channel --
         * which is the honest empty state. A channel that says it is not
         * wired to anything produces no reading, so an empty blob blanks
         * every panel rather than showing something invented. */
        memset(&config_, 0, sizeof(config_));

        for (uint8_t i = 0; i < SENSOR_MAX_CHANNELS; i++)
        {
            config_.sensors[i].channel     = i;
            config_.sensors[i].signal_id   = SENSOR_SIGNAL_NONE;
            config_.sensors[i].band_margin = SENSOR_DEFAULT_BAND_MARGIN;
        }

        config_.header.magic   = DEVICE_CONFIG_MAGIC;
        config_.header.version = DEVICE_CONFIG_VERSION;
    }

    FLASHMEM bool DeviceConfigStore::load()
    {
        rejected_ = false;

        /**
         * Asked once, here, rather than left to surface as a string of
         * failed writes. A store that cannot hold the blob does not
         * degrade gracefully -- it looks exactly like a device that is
         * booting for the first time, forever. */
        too_small_ = backing_.capacity() < (offset_ + sizeof(DeviceConfig));
        if (too_small_)
        {
            clear();
            return false;
        }

        DeviceConfigHeader header {};
        if (!backing_.read(offset_, &header, sizeof(header)))
        {
            clear();
            return false;
        }

        if (header.magic != DEVICE_CONFIG_MAGIC)
        {
            /**
             * Erased storage reads back as 0xFF on both backends, so this
             * is the ordinary first-boot path and not a fault. */
            clear();
            return false;
        }

        /**
         * Everything past here was written by *something* claiming to be
         * this blob, so a disagreement is a real mismatch rather than a
         * blank device -- worth telling the caller apart from first boot,
         * because it means a calibration somebody entered is being
         * discarded. */
        bool shape_ok = header.version == DEVICE_CONFIG_VERSION
                        && header.struct_size == sizeof(DeviceConfig)
                        && header.catalog_count
                               == static_cast<uint8_t>(SIGNAL_CATALOG_COUNT)
                        && header.sensor_channels <= SENSOR_MAX_CHANNELS
                        && header.binding_count <= ActionMap::MAX_BINDINGS;

        if (!shape_ok)
        {
            clear();
            rejected_ = true;
            return false;
        }

        if (!backing_.read(offset_, &config_, sizeof(config_)))
        {
            clear();
            return false;
        }

        return true;
    }

    FLASHMEM void DeviceConfigStore::save()
    {
        config_.header.magic       = DEVICE_CONFIG_MAGIC;
        config_.header.version     = DEVICE_CONFIG_VERSION;
        config_.header.struct_size = sizeof(DeviceConfig);
        config_.header.catalog_count
            = static_cast<uint8_t>(SIGNAL_CATALOG_COUNT);

        backing_.write(offset_, &config_, sizeof(config_));
    }

    void DeviceConfigStore::save_sensor(uint8_t index)
    {
        if (index >= SENSOR_MAX_CHANNELS)
        {
            return;
        }

        backing_.write(offset_ + sensor_offset(index),
                       &config_.sensors[index],
                       sizeof(SensorCal));
    }

    void DeviceConfigStore::save_bindings()
    {
        backing_.write(offset_ + offsetof(DeviceConfig, bindings),
                       &config_.bindings,
                       sizeof(config_.bindings));

        /**
         * The count lives in the header, so a partial binding write has to
         * put that back too or the table and its length disagree -- which
         * on the next boot means either lost bindings or garbage ones. */
        backing_.write(offset_ + offsetof(DeviceConfig, header)
                           + offsetof(DeviceConfigHeader, binding_count),
                       &config_.header.binding_count,
                       sizeof(config_.header.binding_count));
    }

    bool DeviceConfigStore::last_load_rejected() const
    {
        return rejected_;
    }

    bool DeviceConfigStore::storage_too_small() const
    {
        return too_small_;
    }

} // namespace core
