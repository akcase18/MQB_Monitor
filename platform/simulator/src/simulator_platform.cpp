/**
 * @file simulator_platform.cpp
 * @brief Composes the simulator's Platform instance.
 */

#include "platform.hpp"

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#if defined(__APPLE__)
    #include <mach-o/dyld.h>
#endif

#include "simulator_can.hpp"
#include "simulator_clock.hpp"
#include "simulator_config_store.hpp"
#include "simulator_display.hpp"
#include "simulator_relay.hpp"
#include "simulator_storage.hpp"
#include "simulator_touch.hpp"

Platform &platform_get()
{
    static Platform s_platform {
        SimulatorCanBus::instance(), SimulatorDisplay::instance(),
        SimulatorTouch::instance(),  SimulatorConfigStore::instance(),
        SimulatorClock::instance(),  SimulatorRelay::instance(),
        SimulatorStorage::instance(),
    };
    return s_platform;
}

void platform_restart()
{
    /* A dev host has nothing to reset, so the closest equivalent is
     * replacing this process with a fresh copy of the same binary --
     * setup() runs again from scratch, reading the config file the
     * caller just saved. exec skips atexit handlers, so the window
     * position is saved by hand first, the same as on any other exit.
     *
     * Exec rather than spawn-and-exit keeps the same process, so a
     * terminal (or `pio run -t exec`) stays attached to it. The cost is
     * one harmless line from AppKit on the way back up -- "Task policy
     * set failed: 4" -- since the new image tries to set the process's
     * app role again, which macOS only allows once per process. Tested:
     * SDL_Quit() first and POSIX_SPAWN_SETEXEC both still print it. */
    SimulatorDisplay::instance().save_position();

    char path[1024] = {};
#if defined(__APPLE__)
    uint32_t size = sizeof(path);
    bool     ok   = _NSGetExecutablePath(path, &size) == 0;
#else
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    bool    ok  = len > 0;
#endif

    if (ok)
    {
        char *argv[] = { path, nullptr };
        execv(path, argv);
    }

    /* Only reached if the exec itself failed -- quitting beats carrying
     * on in an orientation the window no longer matches. */
    perror("platform_restart: exec failed");
    exit(1);
}

/* No field updates on this board -- see platform.hpp's FIRMWARE section.
 * (SimulatorRelay fakes the update *screens*; nothing is installed.) */
const char *platform_firmware_version()
{
    return "simulator";
}

bool platform_update_path_ready()
{
    return true;
}

void platform_firmware_healthy()
{
}

bool platform_take_failed_update(char *version, size_t cap)
{
    (void)version;
    (void)cap;
    return false;
}

void platform_hang_watchdog(bool enabled)
{
    (void)enabled;
}
