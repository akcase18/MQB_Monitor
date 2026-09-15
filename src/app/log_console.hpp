/**
 * @file log_console.hpp
 * @brief The USB-serial command set for pulling logs off a parked car.
 *
 * A debug interface, not part of how the app is composed, so it lives here
 * rather than in the composition root. Its `#ifdef ARDUINO` lives in the
 * .cpp too -- off Arduino there is no `Serial`, and `poll()` compiles to
 * nothing, so `loop()` no longer carries a board branch to call it.
 *
 * Product-neutral: it needs a core::Logger and the platform's IStorage,
 * both of which every product has.
 */
#pragma once

#include "logger.hpp"

namespace app
{
    namespace log_console
    {

        /**
         * Drains any pending serial input and dispatches complete lines.
         * Call once per main-loop pass. A no-op off Arduino.
         *
         * Commands are deliberately **blocking**: this is meant to run as
         * its own offload action (park, plug in, dump), not interleaved
         * with driving, so freezing acquisition and the GUI for the
         * duration of a dump is fine.
         */
        /**
         * Opens the USB-serial port the commands arrive on. Call once from
         * setup(). A no-op off Arduino, which is what lets the composition
         * root drop its last `#include <Arduino.h>`.
         */
        void begin();

        void poll(core::Logger &logger);

    } // namespace log_console
} // namespace app
