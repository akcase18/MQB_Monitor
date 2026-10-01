/**
 * @file slcan.hpp
 * @brief SLCAN (LAWICEL ASCII), the serial-CAN protocol tools/monitor
 * speaks through python-can's `slcan` backend. Shared by the standalone
 * sniffer firmware (platform/teensy/tools/can_sniffer.cpp) and the dash's
 * sniffer mode (src/main.cpp), so both answer the host identically.
 *
 * Two halves: encode() turns one bus frame into the line streamed to the
 * host, and Session
 * interprets host command lines against an IPort that does the actual bus
 * and serial work. The subset implemented is what python-can's open/close
 * handshake needs plus host transmit:
 *
 *   Sn        set bitrate by index (S6 = 500k, the MQB rate); closed only
 *   O         open the channel, NORMAL mode (can ACK + transmit)
 *   L         open the channel, LISTEN-ONLY (silent; never ACKs/transmits)
 *   C         close the channel
 *   t/T       transmit an 11-bit / 29-bit data frame (ignored in listen-only)
 *   Z0 / Z1   disable / enable the 4-hex-digit millisecond timestamp suffix
 *   V / N     firmware version / serial number query
 *   F         status flags (returns F00 -- no errors tracked)
 *
 * Each accepted command replies CR (0x0D); a rejected one replies BELL
 * (0x07), per the SLCAN convention.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "platform.hpp"

namespace core
{
    namespace slcan
    {
        constexpr char CR   = '\r';
        constexpr char BELL = '\a';

        constexpr uint32_t DEFAULT_BAUD = 500000; /* MQB powertrain CAN */

        /** Longest encoded frame: 'T' + 8 id + 1 dlc + 16 data + 4
         * timestamp + CR = 31. */
        constexpr size_t ENCODED_MAX = 32;

        /** Longest host command line, excluding its CR: a 'T' transmit
         * is 26. */
        constexpr size_t COMMAND_MAX = 39;

        /**
         * Encodes `frame` as a t/T line, CR included.
         *
         * @param out At least ENCODED_MAX chars.
         *
         * @returns Bytes written.
         */
        size_t encode(const SniffFrame &frame, bool timestamps, char *out);

        /** Maps an Sn bitrate index to bits/sec; 0 if unsupported. */
        uint32_t baud_for_index(char idx);

        /** What Session drives. Implemented by whoever owns the bus and
         * the serial port. */
        class IPort
        {
            public:
                virtual ~IPort() = default;

                /** @returns False if the bus couldn't be opened. */
                virtual bool open(uint32_t baud, bool listen_only) = 0;
                virtual void close()                               = 0;

                /** Only called while open in normal (not listen-only)
                 * mode. */
                virtual void transmit(const SniffFrame &frame) = 0;

                virtual void write(const char *data, size_t len) = 0;
        };

        class Session
        {
            public:
                explicit Session(IPort &port);

                /** One host command line, without its terminating CR. */
                void handle(const char *line, size_t len);

                /** Closes the channel (via the port) if open, and returns
                 * baud/timestamps to their defaults. */
                void reset();

                bool is_open() const
                {
                    return open_;
                }

                bool is_listen_only() const
                {
                    return listen_only_;
                }

                bool timestamps() const
                {
                    return timestamps_;
                }

            private:
                void reply_ok();
                void reply_err();
                void handle_transmit(const char *line, size_t len, bool ext);

                IPort   &port_;
                bool     open_        = false;
                bool     listen_only_ = true;
                bool     timestamps_  = false;
                uint32_t baud_        = DEFAULT_BAUD;
        };

    } // namespace slcan
} // namespace core
