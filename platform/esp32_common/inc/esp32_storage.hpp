/**
 * @file esp32_storage.hpp
 * @brief IStorage over LittleFS on the ESP32's internal flash.
 *
 * Log chunk files, nothing else -- config lives in its own NVS-backed
 * region (see esp32_config_store.hpp) precisely so that erasing the
 * logs cannot take the configuration with it.
 *
 * One file open at a time, matching the interface. The single handle is
 * what keeps `IStorage` implementable on media that are not filesystems
 * at all, and core::Logger is written to it.
 *
 * ## Which partition
 *
 * LittleFS.begin() mounts the partition labelled `spiffs` by default, so
 * every board's partition table (platform/<board>/partitions_*.csv) keeps
 * that label even though the filesystem inside it is LittleFS. The
 * partition's size is the board's decision, and core::Logger's ring
 * budget must fit inside it -- the ring does not consult the backend
 * (TODO.md), so a board with a small partition passes a smaller ring to
 * the Logger constructor in its composition root.
 */
#pragma once

#include "platform.hpp"

class Esp32Storage : public IStorage
{
    public:
        static Esp32Storage &instance();

        bool          begin() override;
        bool          is_present() override;
        bool          format() override;
        bool          open(const char *path, StorageFileMode mode) override;
        bool          is_open() const override;
        void          close() override;
        size_t        read(void *buf, size_t len) override;
        size_t        write(const void *buf, size_t len) override;
        bool          exists(const char *path) const override;
        bool          remove(const char *path) override;
        StorageMedium medium() const override;

    private:
        Esp32Storage() = default;

        bool mounted_ = false;
};
