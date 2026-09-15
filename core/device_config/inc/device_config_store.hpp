/**
 * @file device_config_store.hpp
 * @brief Loads and saves core::DeviceConfig over the same IConfigStore
 * core::ConfigStore uses, at a different offset.
 *
 * Deliberately the same shape as core::ConfigStore -- `get()` hands back a
 * mutable reference, `save()` writes the lot, and the partial writers put
 * back exactly one record -- so the two are learned once. What differs is
 * the failure policy, and it differs on purpose.
 *
 * ## It has no defaults of its own
 *
 * `load()` reports whether it found something usable and stops there. It
 * does **not** substitute a factory calibration, because there is no such
 * thing at this layer: which sensor is on channel 0, and what its full
 * scale is, are facts about a particular board and belong to the
 * composition root that knows them -- the same way `src/main.cpp` owns its
 * CAN arbitration IDs.
 *
 * That is the lesson from Phase 6.0, where `ConfigStore::set_defaults()`
 * had six MQB catalog ids baked into shared code and was silently wrong
 * for every other product. Shared code here holds the container and the
 * validity rules; the root holds the numbers.
 *
 * ## Rejection is total
 *
 * A blob whose magic, version, `struct_size` or `catalog_count` disagrees
 * is discarded whole rather than partially trusted. There is no migration
 * path yet and no device has shipped, so "reject and let the root
 * re-seed" is both correct and the only honest option -- a half-read
 * calibration is a gauge reading a plausible wrong number, which is the
 * one failure this product's whole conditioning layer exists to prevent.
 */
#pragma once

#include "device_config.hpp"
#include "platform.hpp"

namespace core
{

    /**
     * Where the device blob starts in the backing store.
     *
     * A fixed address with headroom, not `sizeof(Config)`. Deriving it
     * would move the blob every time the shared schema grew by a field,
     * silently orphaning a calibration somebody entered by hand -- the
     * magic would simply not be where it used to be, and the store would
     * report a fresh device. 1 KB clears both products' `Config` (164
     * bytes for the gauge, 620 for the CAN dashboard) with room to spare,
     * and the static_assert below fails the build rather than the gauge if
     * that ever stops being true.
     */
    constexpr uint32_t DEVICE_CONFIG_OFFSET = 1024;

    class DeviceConfigStore
    {
        public:
            explicit DeviceConfigStore(
                IConfigStore &backing,
                uint32_t      offset = DEVICE_CONFIG_OFFSET);

            DeviceConfig       &get();
            const DeviceConfig &get() const;

            /**
             * Reads the blob and validates its header.
             *
             * @returns True if a usable configuration was loaded. **False
             * means the caller must supply one** -- the in-memory copy is
             * left in a coherent but empty state (every channel
             * Unconfigured, no bindings), never a half-read one.
             */
            bool load();

            /** Stamps the header from the running firmware and writes the
             * whole blob. */
            void save();

            /** Writes back exactly one channel's calibration -- what a web
             * form's per-channel Save wants, without rewriting sixteen
             * hundred bytes to change four. */
            void save_sensor(uint8_t index);

            /** Writes back the action map and its count. */
            void save_bindings();

            /** Resets the in-memory copy to the empty state `load()`
             * leaves behind on rejection. Does not write. */
            void clear();

            /** True if the last load() rejected a blob that was present,
             * as opposed to finding erased storage. Worth telling apart:
             * the first is a configuration somebody entered and lost. */
            bool last_load_rejected() const;

            /**
             * True when the backing store is too small to hold this blob
             * at this offset.
             *
             * Checked because the failure is otherwise invisible and
             * baffling: every write silently fails, every load finds
             * nothing, and the device re-seeds its factory calibration on
             * every single boot while appearing to accept changes. A new
             * board bringing up its own IConfigStore is exactly where
             * that happens, so it is worth one explicit question rather
             * than a week of "why won't my settings save".
             */
            bool storage_too_small() const;

        private:
            IConfigStore &backing_;
            uint32_t      offset_;
            DeviceConfig  config_ {};
            bool          rejected_  = false;
            bool          too_small_ = false;
    };

} // namespace core
