/**
 * @file teensy_storage.hpp
 * @brief Teensy IStorage implementation, backed by the onboard QSPI NOR
 * flash via LittleFS. Kept alongside teensy_sd_card.hpp's Micro-SD
 * implementation -- see teensy_platform.cpp for which is currently active.
 */
#pragma once

#include "platform.hpp"

#include <LittleFS.h>

class TeensyStorage : public IStorage
{
    public:
        static TeensyStorage &instance();

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

    private:
        TeensyStorage() = default;

        mutable LittleFS_QSPIFlash fs_;
        File                       file_;
        bool                       open_ = false;
};
