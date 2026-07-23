/**
 * @file teensy_sd_card.hpp
 * @brief Teensy ISdCard implementation, backed by SdFat (bundled with the
 * Teensy core's SD.h) on the SPI1 bus shared with the touch controller.
 */
#pragma once

#include "platform.hpp"

#include <SdFat.h>

class TeensySdCard : public ISdCard
{
    public:
        static TeensySdCard &instance();

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
        TeensySdCard() = default;

        bool probe_card();

        mutable SdFs sd_;
        FsFile file_;
        bool   present_ = false;
        bool   mounted_ = false;
        SdFileMode file_mode_ = SdFileMode::Read;
};
