/**
 * @file teensy_sd_card.cpp
 * @brief IStorage over SdFat, on the SPI1 bus shared with the touch
 * controller (teensy_touch.cpp). Raw card detection (cardBegin()) and
 * filesystem mounting (volumeBegin()) are kept as separate steps so a
 * blank/corrupted card can be told apart from no card at all.
 */

#include "teensy_sd_card.hpp"

#include <Arduino.h>
#include <SPI.h>

namespace
{
    constexpr uint8_t  SD_CS_PIN               = 2; /* EMC_04, shared SPI1
                                                       * with touch */
    constexpr uint8_t  SD_BEGIN_RETRIES        = 3;
    constexpr uint32_t SD_BEGIN_RETRY_DELAY_MS = 50;
} // namespace

TeensySdCard &TeensySdCard::instance()
{
    static TeensySdCard s_instance;
    return s_instance;
}

bool TeensySdCard::probe_card()
{
    SdSpiConfig cfg(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(25), &SPI1);
    for (uint8_t attempt = 0; attempt < SD_BEGIN_RETRIES; attempt++)
    {
        if (sd_.cardBegin(cfg))
        {
            return true;
        }
        delay(SD_BEGIN_RETRY_DELAY_MS);
    }
    return false;
}

bool TeensySdCard::begin()
{
    present_ = probe_card();
    mounted_ = present_ && sd_.volumeBegin();
    return mounted_;
}

bool TeensySdCard::is_present()
{
    if (!present_)
    {
        present_ = probe_card();
    }
    return present_;
}

bool TeensySdCard::format()
{
    if (file_.isOpen() || !is_present())
    {
        return false;
    }

    if (!sd_.format())
    {
        return false;
    }

    mounted_ = sd_.volumeBegin();
    return mounted_;
}

bool TeensySdCard::open(const char *path, StorageFileMode mode)
{
    if (file_.isOpen() || !mounted_)
    {
        return false;
    }

    oflag_t oflag = O_RDONLY;
    switch (mode)
    {
        case StorageFileMode::Read:
            oflag = O_RDONLY;
            break;

        case StorageFileMode::Write:
            oflag = O_WRONLY | O_CREAT | O_TRUNC;
            break;

        case StorageFileMode::Append:
            oflag = O_WRONLY | O_CREAT | O_APPEND;
            break;
    }

    if (!file_.open(path, oflag))
    {
        return false;
    }

    file_mode_ = mode;
    return true;
}

bool TeensySdCard::is_open() const
{
    return file_.isOpen();
}

void TeensySdCard::close()
{
    file_.close();
}

size_t TeensySdCard::read(void *buf, size_t len)
{
    if (!file_.isOpen() || file_mode_ != StorageFileMode::Read)
    {
        return 0;
    }

    int n = file_.read(buf, len);
    if (n < 0)
    {
        present_ = false;
        return 0;
    }
    return static_cast<size_t>(n);
}

size_t TeensySdCard::write(const void *buf, size_t len)
{
    if (!file_.isOpen() || file_mode_ == StorageFileMode::Read)
    {
        return 0;
    }

    size_t n = file_.write(buf, len);
    if (n != len)
    {
        present_ = false;
    }
    return n;
}

bool TeensySdCard::exists(const char *path) const
{
    if (!mounted_)
    {
        return false;
    }
    return sd_.exists(path);
}

bool TeensySdCard::remove(const char *path)
{
    if (!mounted_)
    {
        return false;
    }
    return sd_.remove(path);
}
