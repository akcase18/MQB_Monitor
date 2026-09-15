#include "log_console.hpp"

#include "mem_placement.hpp"
#include "platform.hpp"

#include <stddef.h>
#include <string.h>

#ifdef ARDUINO
    #include <Arduino.h>
#endif

namespace app
{
    namespace log_console
    {

#ifdef ARDUINO

        namespace
        {
            /**
             * USB-CDC, so the rate is nominal -- it is the number the host
             * side expects to open, not a line speed anything honours. */
            constexpr uint32_t SERIAL_BAUD = 115200;

            /**
             * USB-serial log retrieval -- a small bespoke line command set (not
             * SLCAN; that's can_sniffer.cpp's separate, unrelated firmware/
             * protocol for a different Teensy board entirely), modeled on the
             * same "line assembly, dispatch on CR/LF" shape can_sniffer.cpp
             * already uses for its own USB-CDC command parsing. Deliberately
             * blocking -- this is meant to run as its own dedicated offload
             * action (park, plug in, dump), not interleaved with driving, so
             * freezing everything else (CAN polling, GUI) for the duration is
             * fine. */
            FLASHMEM void handle_log_command(core::Logger &logger,
                                             const char   *cmd,
                                             size_t        len)
            {
                if (len == 7 && memcmp(cmd, "LOGDUMP", 7) == 0)
                {
                    IStorage &storage = platform_get().storage;
                    for (uint16_t i = 0; i < core::CHUNK_COUNT; i++)
                    {
                        char path[core::CHUNK_PATH_LEN];
                        core::chunk_path(i, path);
                        if (!storage.open(path, StorageFileMode::Read))
                        {
                            continue; /* never written -- nothing to stream */
                        }

                        uint8_t buf[256];
                        size_t  n;
                        while ((n = storage.read(buf, sizeof(buf))) > 0)
                        {
                            Serial.write(buf, n);
                        }
                        storage.close();
                    }
                }
                else if (len == 8 && memcmp(cmd, "LOGCLEAR", 8) == 0)
                {
                    /**
                     * Same one call LoggingScreen's erase button makes (see
                     * logging_erase_cb): format the store, then rebuild
                     * rotation state and the index against the empty result,
                     * leaving the capture count honestly at zero. Recording is
                     * deliberately NOT stopped here the way the on-screen erase
                     * stops it -- this command is issued from a host with the
                     * car parked, and silently changing a persisted setting out
                     * from under the user over a debug serial link is a
                     * surprise the GUI's own confirmation popup doesn't have to
                     * be. */
                    logger.erase();
                }
                else if (len == 9 && memcmp(cmd, "LOGSTATUS", 9) == 0)
                {
                    Serial.print("ready=");
                    Serial.print(logger.is_ready() ? 1 : 0);
                    Serial.print(" session=");
                    Serial.print(logger.session_id());
                    Serial.print(" chunk=");
                    Serial.print(logger.current_chunk_index());
                    Serial.print(" capturing=");
                    Serial.print(logger.is_capturing() ? 1 : 0);
                    Serial.print(" logs=");
                    Serial.print(logger.log_count());
                    Serial.print(" recorded_ms=");
                    Serial.println(logger.total_logged_ms());
                }
            }

            void poll_serial(core::Logger &logger)
            {
                static char   cmd[16];
                static size_t cmd_len = 0;

                while (Serial.available() > 0)
                {
                    char c = static_cast<char>(Serial.read());
                    if (c == '\r' || c == '\n')
                    {
                        if (cmd_len > 0)
                        {
                            handle_log_command(logger, cmd, cmd_len);
                            cmd_len = 0;
                        }
                    }
                    else if (cmd_len < sizeof(cmd) - 1)
                    {
                        cmd[cmd_len++] = c;
                    }
                    else
                    {
                        cmd_len = 0; /* overflow -- drop the line */
                    }
                }
            }
        } // namespace

        void begin()
        {
            Serial.begin(SERIAL_BAUD);
        }

        void poll(core::Logger &logger)
        {
            poll_serial(logger);
        }

#else

        void begin()
        {
        }

        void poll(core::Logger &logger)
        {
            /**
             * No Serial off Arduino. Taking the parameter anyway keeps the
             * call sites in setup()/loop() free of a board branch. */
            (void)logger;
        }

#endif

    } // namespace log_console
} // namespace app
