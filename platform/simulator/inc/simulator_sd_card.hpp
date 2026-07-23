/**
 * @file simulator_sd_card.hpp
 * @brief Simulator ISdCard implementation, backed by a host directory
 * standing in for the card.
 */
#pragma once

#include "platform.hpp"

#include <stdio.h>
#include <string>

class SimulatorSdCard : public ISdCard
{
    public:
        static SimulatorSdCard &instance();

        bool   begin() override;
        bool   is_present() override;
        bool   format() override;
        bool   open(const char *path, SdFileMode mode) override;
        bool   is_open() const override;
        void   close() override;
        size_t read(void *buf, size_t len) override;
        size_t write(const void *buf, size_t len) override;
        bool   exists(const char *path) const override;
        bool   remove(const char *path) override;

    private:
        SimulatorSdCard() = default;

        std::string full_path(const char *path) const;

        FILE      *file_      = nullptr;
        bool       present_   = false;
        SdFileMode file_mode_ = SdFileMode::Read;
};
