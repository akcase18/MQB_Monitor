/**
 * @file teensy_storage.cpp
 * @brief IStorage over LittleFS on the Teensy's onboard QSPI NOR flash.
 * Unlike a removable card, a blank/corrupted chip self-heals: LittleFS's
 * own begin() falls back to formatting when mounting fails, so there's no
 * separate present-but-unmounted recovery path to design around here.
 */

#include "teensy_storage.hpp"

TeensyStorage &TeensyStorage::instance()
{
    static TeensyStorage s_instance;
    return s_instance;
}

bool TeensyStorage::begin()
{
    return fs_.begin();
}

bool TeensyStorage::is_present()
{
    return fs_.mediaPresent() || fs_.begin();
}

bool TeensyStorage::format()
{
    if (open_ || !is_present())
    {
        return false;
    }
    return fs_.format();
}

bool TeensyStorage::open(const char *path, StorageFileMode mode)
{
    if (open_ || !is_present())
    {
        return false;
    }

    uint8_t arduino_mode = FILE_READ;
    switch (mode)
    {
        case StorageFileMode::Read:
            arduino_mode = FILE_READ;
            break;

        case StorageFileMode::Write:
            arduino_mode = FILE_WRITE_BEGIN;
            break;

        case StorageFileMode::Append:
            arduino_mode = FILE_WRITE;
            break;
    }

    file_ = fs_.open(path, arduino_mode);
    open_ = static_cast<bool>(file_);
    return open_;
}

bool TeensyStorage::is_open() const
{
    return open_;
}

void TeensyStorage::close()
{
    file_.close();
    open_ = false;
}

size_t TeensyStorage::read(void *buf, size_t len)
{
    return file_.read(buf, len);
}

size_t TeensyStorage::write(const void *buf, size_t len)
{
    return file_.write(buf, len);
}

bool TeensyStorage::exists(const char *path) const
{
    return fs_.exists(path);
}

bool TeensyStorage::remove(const char *path)
{
    return fs_.remove(path);
}
