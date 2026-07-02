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
};

/**
 * Implemented once per supported board. Only 1 implementation is compiled
 * into a given build.
 *
 * @returns The board's Platform instance.
 */
Platform &platform_get();
