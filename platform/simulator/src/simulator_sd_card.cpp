/**
 * @file simulator_sd_card.cpp
 * @brief ISdCard backed by a host directory, standing in for a Micro-SD
 * card. Always present once the directory exists -- the simulator has no
 * removable-media concept to fake, so unlike the real card's
 * present-but-unmounted state (see platform.hpp's ISdCard doc comment),
 * there's nothing here for is_present() to retry.
 */

#include "simulator_sd_card.hpp"

#include <dirent.h>
#include <sys/stat.h>

namespace
{
    constexpr const char *SD_CARD_DIR = "sim_sdcard";
} // namespace

SimulatorSdCard &SimulatorSdCard::instance()
{
    static SimulatorSdCard s_instance;
    return s_instance;
}

std::string SimulatorSdCard::full_path(const char *path) const
{
    std::string result = SD_CARD_DIR;
    result += "/";
    result += path;
    return result;
}

bool SimulatorSdCard::begin()
{
    mkdir(SD_CARD_DIR, 0755);
    present_ = true;
    return present_;
}

bool SimulatorSdCard::is_present()
{
    return present_;
}

bool SimulatorSdCard::format()
{
    if (file_ || !present_)
    {
        return false;
    }

    DIR *dir = opendir(SD_CARD_DIR);
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

bool SimulatorSdCard::open(const char *path, SdFileMode mode)
{
    if (file_ || !present_)
    {
        return false;
    }

    const char *fmode = "rb";
    switch (mode)
    {
        case SdFileMode::Read:
            fmode = "rb";
            break;

        case SdFileMode::Write:
            fmode = "wb";
            break;

        case SdFileMode::Append:
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

bool SimulatorSdCard::is_open() const
{
    return file_ != nullptr;
}

void SimulatorSdCard::close()
{
    if (file_)
    {
        fclose(file_);
        file_ = nullptr;
    }
}

size_t SimulatorSdCard::read(void *buf, size_t len)
{
    if (!file_ || file_mode_ != SdFileMode::Read)
    {
        return 0;
    }
    return fread(buf, 1, len, file_);
}

size_t SimulatorSdCard::write(const void *buf, size_t len)
{
    if (!file_ || file_mode_ == SdFileMode::Read)
    {
        return 0;
    }
    return fwrite(buf, 1, len, file_);
}

bool SimulatorSdCard::exists(const char *path) const
{
    struct stat st;
    return stat(full_path(path).c_str(), &st) == 0;
}

bool SimulatorSdCard::remove(const char *path)
{
    return ::remove(full_path(path).c_str()) == 0;
}
