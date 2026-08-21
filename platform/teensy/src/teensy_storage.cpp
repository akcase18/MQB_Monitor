/**
 * @file teensy_storage.cpp
 * @brief IStorage over LittleFS_Program, a region carved out of the
 * Teensy's own internal program flash (see teensy_storage.hpp's doc
 * comment for why this replaced LittleFS_QSPIFlash). Unlike a removable
 * card, a blank/corrupted region self-heals: LittleFS's own begin() falls
 * back to formatting when mounting fails, so there's no separate
 * present-but-unmounted recovery path to design around here.
 */

#include "teensy_storage.hpp"

namespace
{
    /* This board's flash is ~16MB and current firmware uses well under
     * 1MB, so 2MB leaves generous headroom for firmware growth while
     * still being enough for the signal-logging feature this storage
     * layer exists for (see TODO.md). Must stay the same across
     * begin()/is_present() calls -- LittleFS_Program carves this region
     * out of flash by size, not by a persisted offset. */
    constexpr uint32_t STORAGE_SIZE_BYTES = 2 * 1024 * 1024;
} // namespace

TeensyStorage &TeensyStorage::instance()
{
    static TeensyStorage s_instance;
    return s_instance;
}

bool TeensyStorage::begin()
{
    return fs_.begin(STORAGE_SIZE_BYTES);
}

bool TeensyStorage::is_present()
{
    return fs_.mediaPresent() || fs_.begin(STORAGE_SIZE_BYTES);
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
