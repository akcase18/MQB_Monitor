/**
 * @file simulator_storage.cpp
 * @brief IStorage backed by a host directory, standing in for whichever
 * real backend is active. Always present once the directory exists -- the
 * simulator has no removable-media or blank-chip concept to fake, so
 * there's nothing here for is_present() to retry.
 */

#include "simulator_storage.hpp"

#include <dirent.h>
#include <sys/stat.h>

namespace
{
    constexpr const char *STORAGE_DIR = "sim_storage";
} // namespace

SimulatorStorage &SimulatorStorage::instance()
{
    static SimulatorStorage s_instance;
    return s_instance;
}

std::string SimulatorStorage::full_path(const char *path) const
{
    std::string result = STORAGE_DIR;
    result += "/";
    result += path;
    return result;
}

bool SimulatorStorage::begin()
{
    mkdir(STORAGE_DIR, 0755);
    present_ = true;
    return present_;
}

bool SimulatorStorage::is_present()
{
    return present_;
}

bool SimulatorStorage::format()
{
    if (file_ || !present_)
    {
        return false;
    }

    DIR *dir = opendir(STORAGE_DIR);
    if (!dir)
    {
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (entry->d_name[0] == '.')
        {
            continue; /* skip "." / ".." */
        }
        ::remove(full_path(entry->d_name).c_str());
    }
    closedir(dir);

    return true;
}

bool SimulatorStorage::open(const char *path, StorageFileMode mode)
{
    if (file_ || !present_)
    {
        return false;
    }

    const char *fmode = "rb";
    switch (mode)
    {
        case StorageFileMode::Read:
            fmode = "rb";
            break;

        case StorageFileMode::Write:
            fmode = "wb";
            break;

        case StorageFileMode::Append:
            fmode = "ab";
            break;
    }

    file_ = fopen(full_path(path).c_str(), fmode);
    if (!file_)
    {
        return false;
    }

    file_mode_ = mode;
    return true;
}

bool SimulatorStorage::is_open() const
{
    return file_ != nullptr;
}

void SimulatorStorage::close()
{
    if (file_)
    {
        fclose(file_);
        file_ = nullptr;
    }
}

size_t SimulatorStorage::read(void *buf, size_t len)
{
    if (!file_ || file_mode_ != StorageFileMode::Read)
    {
        return 0;
    }
    return fread(buf, 1, len, file_);
}

size_t SimulatorStorage::write(const void *buf, size_t len)
{
    if (!file_ || file_mode_ == StorageFileMode::Read)
    {
        return 0;
    }
    return fwrite(buf, 1, len, file_);
}

bool SimulatorStorage::exists(const char *path) const
{
    struct stat st;
    return stat(full_path(path).c_str(), &st) == 0;
}

bool SimulatorStorage::remove(const char *path)
{
    return ::remove(full_path(path).c_str()) == 0;
}
