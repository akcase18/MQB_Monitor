/**
 * @file simulator_storage.hpp
 * @brief Simulator IStorage implementation, backed by a host directory
 * standing in for whichever real backend (Micro-SD, onboard flash) is
 * active on the Teensy build.
 */
#pragma once

#include "platform.hpp"

#include <stdio.h>
#include <string>

class SimulatorStorage : public IStorage
{
    public:
        static SimulatorStorage &instance();

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
        SimulatorStorage() = default;

        std::string full_path(const char *path) const;

        FILE           *file_      = nullptr;
        bool            present_   = false;
        StorageFileMode file_mode_ = StorageFileMode::Read;
};
