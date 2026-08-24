/**
 * @file teensy_sd_card.hpp
 * @brief Teensy IStorage implementation, backed by SdFat (bundled with the
 * Teensy core's SD.h) on the SPI1 bus shared with the touch controller.
 * Kept alongside teensy_storage.hpp's onboard-flash implementation for
 * whenever a Micro-SD slot is back in the design -- see teensy_platform.cpp
 * for which is currently active.
 */
#pragma once

#include "platform.hpp"

#include <SdFat.h>

class TeensySdCard : public IStorage
{
    public:
        static TeensySdCard &instance();

        bool   begin() override;
        bool   is_present() override;
        bool   format() override;
        bool   open(const char *path, StorageFileMode mode) override;
        bool   is_open() const override;
        void   close() override;
        size_t read(void *buf, size_t len) override;
        size_t write(const void *buf, size_t len) override;
        bool   exists(const char *path) const override;
        bool   remove(const char *path) override;

        StorageMedium medium() const override;

    private:
        TeensySdCard() = default;

        bool probe_card();

        mutable SdFs sd_;
        FsFile file_;
        bool   present_ = false;
        bool   mounted_ = false;
        StorageFileMode file_mode_ = StorageFileMode::Read;
};
