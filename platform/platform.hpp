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

/*==============================================================================
 * CAN
 =============================================================================*/

struct CanFrame
{
        uint32_t id;
        uint8_t  len;
        uint8_t  data[8];
};

/**
 * One frame as seen on the wire in sniffer mode -- unlike CanFrame, which
 * only ever carries the dash's own 11-bit UDS traffic, a sniffed frame can
 * be extended or remote, and carries a capture time for SLCAN's optional
 * timestamp suffix. 16 bytes, so a ring of them packs evenly.
 */
struct SniffFrame
{
        uint32_t id;
        uint16_t ts_ms;
        uint8_t  len;
        uint8_t  flags; /* SNIFF_FLAG_* */
        uint8_t  data[8];
};

constexpr uint8_t SNIFF_FLAG_EXTENDED = 0x01;
constexpr uint8_t SNIFF_FLAG_REMOTE   = 0x02;

struct SniffStats
{
        /** Frames taken off the bus since sniff_begin(). */
        uint32_t rx;

        /** Frames lost because the sniff ring was full -- the host fell
         * behind. Nonzero means the capture has gaps. */
        uint32_t dropped;
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

        /*
         * Sniffer mode -- an opt-in that boards without it never have to
         * think about: every default below reports "unsupported".
         *
         * sniff_begin() reconfigures the bus to accept every frame and
         * buffers them for sniff_read(); send()/receive() must not be used
         * until sniff_end(), which puts back exactly what begin() set up.
         */

        /**
         * @param baud        Bus baud rate, in bits/sec.
         * @param listen_only True never ACKs or transmits -- a silent third
         *                    node. False joins the bus normally so
         *                    sniff_send() works.
         *
         * @returns False if this board can't sniff.
         */
        virtual bool sniff_begin(uint32_t baud, bool listen_only)
        {
            (void)baud;
            (void)listen_only;
            return false;
        }

        virtual void sniff_end() {}

        /** Non-blocking; oldest buffered frame first. */
        virtual bool sniff_read(SniffFrame &out)
        {
            (void)out;
            return false;
        }

        /** Ignored (returns false) when sniffing listen-only. */
        virtual bool sniff_send(const SniffFrame &frame)
        {
            (void)frame;
            return false;
        }

        virtual SniffStats sniff_stats() const
        {
            return SniffStats{0, 0};
        }
};

/*==============================================================================
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
         * Rotates the panel to match the given orientation, plus a further
         * 180 degrees when `flipped` -- the same panel mounted the other
         * way up. Changes what get_resolution() reports (width/height
         * swap between Landscape and Portrait; `flipped` never changes
         * it). Called at boot, right after begin(). A Landscape/Portrait
         * change still only takes effect on the next boot (the
         * lv_display_t is sized once), but `flipped` alone may be changed
         * live by calling this again with the same orientation -- the
         * caller owns hiding and redrawing the frame that lands on the
         * panel mid-change.
         */
        virtual void set_orientation(DisplayOrientation orientation,
                                     bool               flipped)
            = 0;

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
         * Whole-screen render targets, if the board has them. A display
         * that returns true owns two frame buffers of at least
         * width*height RGB565 pixels each (post-rotation) and wants LVGL
         * rendering straight into them (LV_DISPLAY_RENDER_MODE_DIRECT)
         * rather than into main.cpp's banded partial buffers. Under that
         * mode flush()'s `pixels` is the *base of the whole frame
         * buffer*, the coordinates are the area within it that changed,
         * and the display may hold the push back until flush_end() so a
         * pass goes to the panel as one transfer. Called once, at boot.
         *
         * Default: none; main.cpp then renders in bands.
         */
        virtual bool get_frame_buffers(uint16_t *&buf_1,
                                       uint16_t *&buf_2,
                                       size_t    &bytes)
        {
            buf_1 = nullptr;
            buf_2 = nullptr;
            bytes = 0;
            return false;
        }

        /**
         * Marks the end of one render pass: every flush() for it has
         * been issued. Called on every platform; only a display that
         * defers its pushes (see get_frame_buffers) has anything to do.
         */
        virtual void flush_end()
        {
        }

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

/*==============================================================================
 * TOUCH
 =============================================================================*/

struct TouchPoint
{
        int16_t x;
        int16_t y;
        bool    pressed;
};

/**
 * Rotates an already-oriented touch point 180 degrees within a
 * `width` x `height` screen -- what every ITouch applies on top of its
 * own Landscape/Portrait transform when set_orientation() says `flipped`.
 */
inline void flip_touch_point(TouchPoint &point, uint16_t width, uint16_t height)
{
    point.x = static_cast<int16_t>((width - 1) - point.x);
    point.y = static_cast<int16_t>((height - 1) - point.y);
}

class ITouch
{
    public:
        virtual ~ITouch() = default;

        virtual void begin() = 0;

        /**
         * Tells the touch driver which way the panel is currently
         * rotated, so raw controller coordinates can be remapped to
         * match. Called at boot, right after begin(), and again live
         * whenever IDisplay::set_orientation() is -- same arguments, same
         * rules.
         */
        virtual void set_orientation(DisplayOrientation orientation,
                                     bool               flipped)
            = 0;

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
        IStorage     &storage;
};

/**
 * Implemented once per supported board. Only 1 implementation is compiled
 * into a given build.
 *
 * @returns The board's Platform instance.
 */
Platform &platform_get();

/**
 * Restarts the board from the top, as if power-cycled -- how an
 * orientation change (boot-time-only, see IDisplay::set_orientation)
 * takes effect without the user pulling power. Never returns. Anything
 * that must survive has to be persisted first; IConfigStore writes are
 * durable on return, so a ConfigStore::save() right before is enough.
 */
[[noreturn]] void platform_restart();

/*==============================================================================
 * FIRMWARE
 *
 * Hooks for firmware updates taken in the field. Only the ESP32-S3 dash
 * has them (A/B app slots with bootloader rollback -- see
 * platform/dash_s3/OTA_PLAN.md); on every other board each is a no-op
 * that reports nothing to do.
 =============================================================================*/

/** Version strings, NUL included, are at most this long. */
constexpr size_t FIRMWARE_VERSION_LEN = 32;

/** The running firmware's version, for showing the user. Never null. */
const char *platform_firmware_version();

/**
 * Declares the running firmware good. A freshly installed update boots
 * on probation: any reset before this is called -- a crash, the hang
 * watchdog, the power going -- boots the previous firmware instead. Call
 * once the app has been running normally long enough to be trusted.
 * Harmless to call again, or on firmware that wasn't just installed.
 */
void platform_firmware_healthy();

/**
 * Reports an update that didn't take: the board installed one, and then
 * came back up on its previous firmware (the new one never declared
 * itself healthy, or failed to boot at all). True once, on the first
 * call after such a boot, with the version that failed copied into
 * `version` (`cap` bytes, always terminated; empty if it wasn't
 * recorded); false ever after.
 */
bool platform_take_failed_update(char *version, size_t cap);

/**
 * Turns the main-loop hang watchdog on or off. While it's on, loop() has
 * to come back around within a few seconds or the board resets, which
 * is what turns an update that hangs into a rollback rather than a dead
 * dash. Turn it off around anything that blocks on purpose for longer --
 * a full log dump or erase -- and back on afterwards.
 */
void platform_hang_watchdog(bool enabled);
