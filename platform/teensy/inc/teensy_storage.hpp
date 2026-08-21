/**
 * @file teensy_storage.hpp
 * @brief Teensy IStorage implementation, backed by a region of the MCU's
 * own program flash via LittleFS_Program. Originally used LittleFS_
 * QSPIFlash (an external NOR chip on Teensy 4.1's own PCB, wired to the
 * IMXRT1062's FlexSPI2 peripheral) -- that hung on real hardware here
 * (confirmed by bisect), almost certainly because the MicroMod Teensy
 * processor module doesn't have that same external chip wired to
 * FlexSPI2, and the underlying library's register-polling loops in
 * begin() have no timeout against a peripheral that never responds.
 * LittleFS_Program instead reserves a chunk of the chip's own internal
 * flash (see STORAGE_SIZE_BYTES in the .cpp) -- guaranteed present on any
 * Teensy 4.x regardless of carrier board, no external wiring to depend
 * on. Kept alongside teensy_sd_card.hpp's Micro-SD implementation -- see
 * teensy_platform.cpp for which is currently active.
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

        mutable LittleFS_Program fs_;
        File                     file_;
        bool                     open_ = false;
};
