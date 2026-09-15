/**
 * @file esp32s3_storage.hpp
 * @brief IStorage over LittleFS on the S3's internal flash.
 *
 * Log chunk files, nothing else -- config lives in its own NVS-backed
 * region (see esp32s3_config_store.hpp) precisely so that erasing the
 * logs cannot take the calibration with it.
 *
 * One file open at a time, matching the interface. The single handle is
 * what keeps `IStorage` implementable on media that are not filesystems
 * at all, and core::Logger is written to it.
 *
 * ## The log ring does not size itself to the partition
 *
 * core::CHUNK_COUNT x CHUNK_SIZE_BYTES is a compile-time 13MiB budget
 * that does not consult the backend -- already recorded in TODO.md. On
 * this board the LittleFS partition is a few megabytes, so
 * src/main_gauge.cpp passes core::Logger a smaller ring explicitly rather
 * than letting it try to allocate a ring the flash cannot hold. With
 * three signals instead of six that ring still covers far more driving
 * than the CAN product's does.
 */
#pragma once

#include "platform.hpp"

class Esp32s3Storage : public IStorage
{
    public:
        static Esp32s3Storage &instance();

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
        Esp32s3Storage() = default;

        bool mounted_ = false;
};
