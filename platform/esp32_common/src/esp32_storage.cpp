#include "esp32_storage.hpp"

#include <Arduino.h>
#include <LittleFS.h>

namespace
{
    /**
     * The one open handle. A file object rather than a pointer so there is
     * nothing to leak; `operator bool` is how LittleFS reports whether it
     * refers to anything. */
    File s_file;

    /**
     * Formats on a failed mount. First boot after flashing has no
     * filesystem at all, and refusing to create one would mean logging
     * never worked until somebody ran a format by hand. */
    constexpr bool FORMAT_ON_FAIL = true;
} // namespace

Esp32Storage &Esp32Storage::instance()
{
    static Esp32Storage s_instance;
    return s_instance;
}

bool Esp32Storage::begin()
{
    if (mounted_)
    {
        return true;
    }

    mounted_ = LittleFS.begin(FORMAT_ON_FAIL);
    return mounted_;
}

bool Esp32Storage::is_present()
{
    /**
     * Not const, and re-attempts the mount: the interface allows this
     * because a removable medium can appear later. Internal flash cannot,
     * but a first mount that failed for a transient reason can succeed on
     * a retry, and the caller gets the same answer either way. */
    if (!mounted_)
    {
        begin();
    }
    return mounted_;
}

bool Esp32Storage::format()
{
    if (s_file)
    {
        /**
         * Refuse rather than format out from under an open handle -- the
         * interface says close() first, and core::Logger does. */
        return false;
    }

    LittleFS.end();
    mounted_ = false;

    if (!LittleFS.format())
    {
        return false;
    }

    mounted_ = LittleFS.begin(FORMAT_ON_FAIL);
    return mounted_;
}

bool Esp32Storage::open(const char *path, StorageFileMode mode)
{
    if (!mounted_ || s_file || path == nullptr)
    {
        return false;
    }

    const char *arduino_mode = FILE_READ;
    switch (mode)
    {
        case StorageFileMode::Write:
            arduino_mode = FILE_WRITE; /* creates/truncates */
            break;
        case StorageFileMode::Append:
            arduino_mode = FILE_APPEND;
            break;
        case StorageFileMode::Read:
        default:
            arduino_mode = FILE_READ;
            break;
    }

    if (mode == StorageFileMode::Read && !LittleFS.exists(path))
    {
        /**
         * Read of a missing file fails rather than creating an empty one
         * -- core::Logger distinguishes "this chunk does not exist yet"
         * from "this chunk is empty", and an auto-created file would make
         * them the same. */
        return false;
    }

    s_file = LittleFS.open(path, arduino_mode);
    return static_cast<bool>(s_file);
}

bool Esp32Storage::is_open() const
{
    return static_cast<bool>(s_file);
}

void Esp32Storage::close()
{
    if (s_file)
    {
        s_file.close();
    }
}

size_t Esp32Storage::read(void *buf, size_t len)
{
    if (!s_file || buf == nullptr)
    {
        return 0;
    }
    return s_file.read(static_cast<uint8_t *>(buf), len);
}

size_t Esp32Storage::write(const void *buf, size_t len)
{
    if (!s_file || buf == nullptr)
    {
        return 0;
    }
    return s_file.write(static_cast<const uint8_t *>(buf), len);
}

bool Esp32Storage::exists(const char *path) const
{
    if (!mounted_ || path == nullptr)
    {
        return false;
    }
    return LittleFS.exists(path);
}

bool Esp32Storage::remove(const char *path)
{
    if (!mounted_ || path == nullptr)
    {
        return false;
    }
    return LittleFS.remove(path);
}

StorageMedium Esp32Storage::medium() const
{
    return StorageMedium::Flash;
}
