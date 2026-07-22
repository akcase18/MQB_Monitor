/**
 * @file simulator_serial_link.cpp
 * @brief ISerialLink over a real host serial port (POSIX termios), not a
 * fabricated stand-in — unlike SimulatorCanBus, there's no real bus to
 * fake here; a USB-serial adapter on the host talking to a real companion
 * ESP32-C3 is exactly the same kind of "real I/O to a real peripheral"
 * SimulatorDisplay/SimulatorTouch already do against a real SDL
 * window/mouse. The device path comes from the MQB_REMOTE_SERIAL_PORT
 * environment variable, read once in begin() — unset (the default) falls
 * back to a permanent no-data no-op, so plain GUI work on `pio run
 * -e simulator` is unaffected.
 */

#include "simulator_serial_link.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

namespace
{
    /* Standard POSIX termios speed constants only — higher rates
     * (460800/921600) need macOS's non-portable IOSSIOSPEED ioctl instead
     * of a Bxxxxxx constant, not worth the platform-specific code for a
     * desktop dev tool. */
    speed_t baud_to_speed(uint32_t baud)
    {
        switch (baud)
        {
            case 230400:
                return B230400;
            case 115200:
                return B115200;
            default:
                return B115200;
        }
    }
} // namespace

SimulatorSerialLink &SimulatorSerialLink::instance()
{
    static SimulatorSerialLink s_instance;
    return s_instance;
}

void SimulatorSerialLink::begin(uint32_t baud)
{
    const char *path = getenv("MQB_REMOTE_SERIAL_PORT");
    if (path == nullptr)
    {
        fd_ = -1; /* no C3 configured — permanent no-op */
        return;
    }

    fd_ = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0)
    {
        return;
    }

    termios tty {};
    if (tcgetattr(fd_, &tty) != 0)
    {
        close(fd_);
        fd_ = -1;
        return;
    }

    cfmakeraw(&tty);
    speed_t speed = baud_to_speed(baud);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);
    tty.c_cc[VMIN]  = 0; /* non-blocking read: return immediately with */
    tty.c_cc[VTIME] = 0; /* whatever bytes are already available (or none) */

    tcsetattr(fd_, TCSANOW, &tty);
}

size_t SimulatorSerialLink::read(uint8_t *buf, size_t len)
{
    if (fd_ < 0)
    {
        return 0;
    }

    ssize_t n = ::read(fd_, buf, len);
    return n > 0 ? static_cast<size_t>(n) : 0;
}

size_t SimulatorSerialLink::write(const uint8_t *buf, size_t len)
{
    if (fd_ < 0)
    {
        return 0;
    }

    /* fd_ is O_NONBLOCK, so a single ::write() isn't guaranteed to accept
     * all `len` bytes -- it can legitimately do a short write once `len`
     * exceeds whatever room is currently free in the kernel's serial
     * output buffer, silently truncating the frame if the caller doesn't
     * loop (RemoteConfigLink::write_frame() doesn't check the return
     * value at all). Never an issue for the small command/event frames
     * this link originally only ever carried, but RESP_STATE's
     * ~1223-byte payload is large enough to trigger it. Retry on
     * EAGAIN/EWOULDBLOCK until every byte is actually written or a real
     * error occurs. */
    size_t written = 0;
    while (written < len)
    {
        ssize_t n = ::write(fd_, buf + written, len - written);
        if (n > 0)
        {
            written += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            usleep(500);
            continue;
        }
        break;
    }
    return written;
}
