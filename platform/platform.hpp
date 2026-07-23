/**
 * @file platform.hpp
 * @brief Hardware abstraction interfaces. Implemented once per supported
 * board under platform/<board>/. Exactly one implementation is linked into
 * a given build (see platformio.ini's per-env build_src_filter) so nothing
 * outside platform/ ever needs an #ifdef to pick a board.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/*==============================================================================
 * CAN
 =============================================================================*/

struct CanFrame
{
        uint32_t id;
        uint8_t  len;
        uint8_t  data[8];
};

class ICanBus
{
    public:
        virtual ~ICanBus() = default;

        /**
         * One-time peripheral initialization.
         *
         * @param tx_id CAN ID this bus sends requests on.
         * @param rx_id CAN ID this bus filters responses to.
         * @param baud  Bus baud rate, in bits/sec.
         *
         * @returns True on success, otherwise false.
         */
        virtual bool begin(uint32_t tx_id, uint32_t rx_id, uint32_t baud) = 0;

        /**
         * Non-blocking send function.
         *
         * @param frame CAN frame to send.
         *
         * @returns True on success, otherwise false.
         */
        virtual bool send(const CanFrame &frame) = 0;

        /**
         * Non-blocking receive function. Will fill `out` frame if data is
         * available.
         *
         * @param out CAN frame to put received data into.
         *
         * @returns True if data is received, otherwise false.
         */
        virtual bool receive(CanFrame &out) = 0;
};

/*==============================================================================
 * DISPLAY
 =============================================================================*/

class IDisplay
{
    public:
        virtual ~IDisplay() = default;

        /**
         * One-time peripheral initialization.
         */
        virtual void begin() = 0;

        /**
         * Reports the panel's native resolution.
         */
        virtual void get_resolution(uint16_t &width, uint16_t &height) const
            = 0;

        /**
         * Sends the given pixel area to the display to write.
         *
         * @param x1     First x-coordinate, inclusive.
         * @param y1     First y-coordinate, inclusive.
         * @param x2     Second x-coordinate, inclusive.
         * @param y2     Second y-coordinate, inclusive.
         * @param pixels RGB565 pixel buffer to flush, (x2-x1+1)*(y2-y1+1)
         * entries.
         */
        virtual void flush(int32_t         x1,
                           int32_t         y1,
                           int32_t         x2,
                           int32_t         y2,
                           const uint16_t *pixels) = 0;

        /**
         * Used to determine when the transfer started by flush() has
         * completed.
         *
         * @returns True when transfer is complete. Otherwise false.
         */
        virtual bool flush_ready() const = 0;

        /**
         * Sets backlight brightness.
         *
         * @param pct Brightness, 0-100.
         */
        virtual void set_backlight(uint8_t pct) = 0;
};

/*==============================================================================
 * TOUCH
 =============================================================================*/

struct TouchPoint
{
        int16_t x;
        int16_t y;
        bool    pressed;
};

class ITouch
{
    public:
        virtual ~ITouch() = default;

        virtual void begin() = 0;

        /**
         * Polls touch controller. Fills `out` if data is available.
         *
         * @param out Touch point to put data into.
         *
         * @returns True if touch is detected, otherwise false.
         */
        virtual bool read(TouchPoint &out) = 0;
};

/*==============================================================================
 * CONFIG STORE
 =============================================================================*/

/**
 * Raw persistent byte storage. core/config owns the actual schema and
 * versioning on top of this; this interface only knows about bytes.
 */
class IConfigStore
{
    public:
        virtual ~IConfigStore() = default;

        virtual bool   read(uint32_t offset, void *buf, size_t len)        = 0;
        virtual bool   write(uint32_t offset, const void *buf, size_t len) = 0;
        virtual size_t capacity() const                                    = 0;
};

/*==============================================================================
 * SERIAL LINK
 =============================================================================*/

/**
 * Raw, non-blocking byte transport to a companion board (e.g. an ESP32-C3
 * running the wireless-configuration front end). remote/protocol owns
 * framing/CRC on top of this; this interface only knows about bytes, same
 * relationship IConfigStore has to core/config's schema.
 */
class ISerialLink
{
    public:
        virtual ~ISerialLink() = default;

        /**
         * One-time peripheral initialization.
         *
         * @param baud Link baud rate, in bits/sec.
         */
        virtual void begin(uint32_t baud) = 0;

        /**
         * Non-blocking read. Reads up to `len` bytes without blocking.
         *
         * @param buf Buffer to read into.
         * @param len Maximum number of bytes to read.
         *
         * @returns Number of bytes actually read (0 if none available).
         */
        virtual size_t read(uint8_t *buf, size_t len) = 0;

        /**
         * Non-blocking write.
         *
         * @param buf Bytes to write.
         * @param len Number of bytes to write.
         *
         * @returns Number of bytes actually written.
         */
        virtual size_t write(const uint8_t *buf, size_t len) = 0;
};

/*==============================================================================
 * SD CARD
 =============================================================================*/

enum class SdFileMode
{
        Read,  /* Existing file; fails if missing. */
        Write, /* Create/truncate from scratch. */
        Append /* Create if missing, else write from end-of-file. */
};

/**
 * Raw file I/O on a removable Micro-SD card. One file open at a time,
 * matching ICanBus/ISerialLink's single-stream style rather than a
 * multi-handle filesystem API. Neither board wires a card-detect pin, so
 * presence comes from actually attempting SPI communication with a card
 * (retried a few times -- see is_present()), not a live GPIO signal.
 * "Present" is deliberately independent of "has a valid filesystem": a
 * blank/corrupted card still reads as present (so it can be formatted)
 * rather than looking identical to no card at all -- see format().
 */
class ISdCard
{
    public:
        virtual ~ISdCard() = default;

        /**
         * Initializes raw SPI communication with the card and mounts its
         * filesystem.
         *
         * @returns True only if both succeed -- a present-but-unformatted
         * or corrupted card returns false here even though is_present()
         * is true; call format(), which re-mounts on success.
         */
        virtual bool begin() = 0;

        /**
         * Whether raw SPI communication with a card has succeeded. Cheap
         * when already true (returns the cached flag, demoted to false by
         * a failed read()/write() -- a card pulled mid-session is only
         * discovered that way, never proactively). When false, re-attempts
         * the same retry-probe begin() uses, so a freshly-inserted card is
         * picked up without a dedicated rescan call. Not const -- it can
         * trigger real hardware I/O.
         */
        virtual bool is_present() = 0;

        /**
         * Wipes the card and creates a fresh filesystem, then re-mounts
         * it. Two distinct callers, both real, not just an error path:
         * (1) recovery for a blank/corrupted card that failed begin()'s
         * volume-mount step (is_present() true, begin() false), and (2)
         * the intended everyday way to reclaim space -- e.g. once logs
         * have been offloaded to a computer, format() clears everything
         * before logging starts again, on an otherwise perfectly healthy,
         * already-mounted card. Requires is_present() but NOT a prior
         * successful begin(). Fails if a file is currently open (close()
         * it first). Destroys all existing data.
         */
        virtual bool format() = 0;

        /**
         * Opens `path` in `mode`. Fails if a file's already open (close()
         * it first) or the card isn't mounted.
         */
        virtual bool open(const char *path, SdFileMode mode) = 0;

        virtual bool is_open() const = 0;
        virtual void close()         = 0;

        /**
         * @returns Bytes actually read (0 at EOF or on failure).
         */
        virtual size_t read(void *buf, size_t len) = 0;

        /**
         * @returns Bytes actually written (< len signals failure, e.g.
         * the card having been pulled).
         */
        virtual size_t write(const void *buf, size_t len) = 0;

        virtual bool exists(const char *path) const = 0;
        virtual bool remove(const char *path)        = 0;
};

/*==============================================================================
 * CLOCK
 =============================================================================*/

class IClock
{
    public:
        virtual ~IClock() = default;

        /**
         * @returns Monotonic milliseconds since boot.
         */
        virtual uint32_t millis() const = 0;
};

/*==============================================================================
 * PLATFORM
 =============================================================================*/

/**
 * 1 instance per board.
 */
struct Platform
{
        ICanBus      &can;
        IDisplay     &display;
        ITouch       &touch;
        IConfigStore &config_store;
        IClock       &clock;
        ISerialLink  &serial;
        ISdCard      &sd_card;
};

/**
 * Implemented once per supported board. Only 1 implementation is compiled
 * into a given build.
 *
 * @returns The board's Platform instance.
 */
Platform &platform_get();
