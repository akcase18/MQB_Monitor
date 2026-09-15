/**
 * @file platform.hpp
 * @brief Hardware abstraction interfaces. Implemented once per supported
 * board under platform/<board>/. Exactly one implementation is linked into
 * a given build (see platformio.ini's per-env build_src_filter) so nothing
 * outside platform/ ever needs an #ifdef to pick a board. IStorage is an
 * exception: platform/teensy compiles two implementations (Micro-SD and
 * onboard flash) simultaneously, switched via a single #define in
 * teensy_platform.cpp -- see that file.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * ==============================================================================
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

/**
 * ==============================================================================
 * DISPLAY
 =============================================================================*/

enum class DisplayOrientation
{
    Landscape,
    Portrait
};

class IDisplay
{
    public:
        virtual ~IDisplay() = default;

        /**
         * One-time peripheral initialization.
         */
        virtual void begin() = 0;

        /**
         * Rotates the panel to match the given orientation. Changes what
         * get_resolution() reports (width/height swap). Called once at
         * boot, right after begin() -- orientation changes take effect
         * on next boot, not live.
         */
        virtual void set_orientation(DisplayOrientation orientation) = 0;

        /**
         * Reports the panel's current resolution (post-rotation).
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
         * Sets backlight brightness.
         *
         * @param pct Brightness, 0-100.
         */
        virtual void set_backlight(uint8_t pct) = 0;

        /**
         * Kills the backlight entirely -- a true off, which
         * set_backlight(0) deliberately is not: the user-facing 0-100%
         * brightness scale floors at a minimum duty because "0%" there
         * means "as dim as it gets", not "blank the dashboard".
         *
         * For hiding a transition the panel shouldn't be showing at all
         * (boot, before the first frame has landed in GRAM; an accent
         * change, which tears every screen down and rebuilds it). Undone
         * by calling set_backlight() again -- there is no
         * remembered-brightness state down here, the caller owns that.
         */
        virtual void set_backlight_off() = 0;
};

/**
 * ==============================================================================
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
         * Tells the touch driver which way the panel is currently
         * rotated, so raw controller coordinates can be remapped to
         * match. Called once at boot, right after begin() -- orientation
         * changes take effect on next boot, not live.
         */
        virtual void set_orientation(DisplayOrientation orientation) = 0;

        /**
         * Polls touch controller. Fills `out` if data is available.
         *
         * @param out Touch point to put data into.
         *
         * @returns True if touch is detected, otherwise false.
         */
        virtual bool read(TouchPoint &out) = 0;
};

/**
 * ==============================================================================
 * BUTTONS
 =============================================================================*/

/**
 * Momentary push buttons, addressed by index. Raw level only -- no
 * debounce, no timing, no interpretation of a press as short/long/double.
 * All of that is core/input's job, where it is host-testable against a
 * synthetic clock instead of needing a finger and a stopwatch.
 *
 * Indexed rather than one interface instance per button so a board with
 * one, two or no buttons is the same code path everywhere above here. A
 * board with no buttons supplies NullButtons (platform/null_buttons.hpp),
 * whose count() is 0.
 */
class IButtons
{
    public:
        virtual ~IButtons() = default;

        virtual void begin() = 0;

        /** How many buttons this board has. Compile-time constant per
         * implementation; may be 0. */
        virtual uint8_t count() const = 0;

        /**
         * @param idx Button index, 0 .. count()-1.
         *
         * @returns True while the button is physically held. Out-of-range
         * indices read as not-pressed, so a caller written for two buttons
         * behaves sanely on a one-button board.
         */
        virtual bool is_down(uint8_t idx) const = 0;
};

/**
 * ==============================================================================
 * ANALOG INPUT
 =============================================================================*/

/**
 * A multi-channel ADC, reporting raw measured voltage and nothing else.
 *
 * Deliberately knows nothing about what is wired to a channel. Divider
 * ratios, sensor transfer functions, thermistor curves, filtering and
 * plausibility limits are all core/sensors' job, where they are plain math
 * over plain numbers and can be unit-tested on the host against known
 * inputs -- which matters, because that is where a wrong number on a gauge
 * would come from.
 *
 * The unit is microvolts, not millivolts: an ADS1115 at its +/-4.096V range
 * resolves 125uV per LSB, and reporting whole millivolts would throw away
 * most of that before core/sensors ever saw it.
 */
class IAnalogInput
{
    public:
        virtual ~IAnalogInput() = default;

        virtual void begin() = 0;

        /** How many channels this board has. May be 0. */
        virtual uint8_t channel_count() const = 0;

        /**
         * Reads one channel's voltage at the ADC pin -- i.e. after any
         * external divider, which core/sensors undoes from its configured
         * resistor values.
         *
         * @param ch  Channel index, 0 .. channel_count()-1.
         * @param out Measured voltage in microvolts. Untouched on failure.
         *
         * @returns False if the channel is out of range or the conversion
         * failed (bus error, timeout). A failed read is NOT the same as a
         * valid reading of 0V, and callers must not treat it as one: 0V is
         * a plausible sensor output, while a failure means the value is
         * unknown. Not const -- a real implementation does bus I/O here.
         */
        virtual bool read_microvolts(uint8_t ch, uint32_t &out) = 0;
};

/**
 * ==============================================================================
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

/**
 * ==============================================================================
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

/**
 * ==============================================================================
 * STORAGE
 =============================================================================*/

enum class StorageFileMode
{
        Read,  /* Existing file; fails if missing. */
        Write, /* Create/truncate from scratch. */
        Append /* Create if missing, else write from end-of-file. */
};

/**
 * Which physical medium is behind an IStorage. Purely informational --
 * nothing in core/ or gui/ branches on it; it exists so a status screen
 * can tell the user where their logs are actually going, which differs
 * per build and (on a board with a card slot) per boot.
 */
enum class StorageMedium
{
        Flash,        /* The MCU's own internal program flash. */
        SdCard,       /* A removable Micro-SD card. */
        HostDirectory /* A plain directory -- the desktop simulator. */
};

/**
 * Raw file I/O on the board's log storage. One file open at a time,
 * matching ICanBus/ISerialLink's single-stream style rather than a
 * multi-handle filesystem API. Deliberately medium-agnostic: the Teensy has
 * two implementations of this (onboard QSPI flash and Micro-SD, see
 * teensy_platform.cpp for which is active), and nothing above platform/
 * needs to know or care which.
 */
class IStorage
{
    public:
        virtual ~IStorage() = default;

        /**
         * Initializes the backing store and mounts its filesystem.
         *
         * @returns True on success.
         */
        virtual bool begin() = 0;

        /**
         * Whether the backing store is ready for use. Not const -- it can
         * trigger hardware I/O and may re-attempt begin() when not yet
         * ready.
         */
        virtual bool is_present() = 0;

        /**
         * Wipes the store and creates a fresh filesystem, then re-mounts
         * it. The intended everyday way to reclaim space -- e.g. once logs
         * have been offloaded to a computer, format() clears everything
         * before logging starts again. Fails if a file is currently open
         * (close() it first). Destroys all existing data.
         */
        virtual bool format() = 0;

        /**
         * Opens `path` in `mode`. Fails if a file's already open (close()
         * it first) or the store isn't mounted.
         */
        virtual bool open(const char *path, StorageFileMode mode) = 0;

        virtual bool is_open() const = 0;
        virtual void close()         = 0;

        /**
         * @returns Bytes actually read (0 at EOF or on failure).
         */
        virtual size_t read(void *buf, size_t len) = 0;

        /**
         * @returns Bytes actually written (< len signals failure).
         */
        virtual size_t write(const void *buf, size_t len) = 0;

        virtual bool exists(const char *path) const = 0;
        virtual bool remove(const char *path)        = 0;

        /** Which medium is behind this store. Compile-time constant per
         * implementation; see StorageMedium. */
        virtual StorageMedium medium() const = 0;
};

/**
 * ==============================================================================
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

/**
 * ==============================================================================
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
        IButtons     &buttons;
        IAnalogInput &analog;
        IConfigStore &config_store;
        IClock       &clock;
        ISerialLink  &serial;
        IStorage     &storage;
};

/**
 * Implemented once per supported board. Only 1 implementation is compiled
 * into a given build.
 *
 * @returns The board's Platform instance.
 */
Platform &platform_get();
