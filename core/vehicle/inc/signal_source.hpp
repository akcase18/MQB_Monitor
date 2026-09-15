/**
 * @file signal_source.hpp
 * @brief Where a dashboard's live values come from, as an interface.
 *
 * One implementation per acquisition method: core/can's UdsSignalSource
 * (requests over a vehicle bus) today, an analog sensor source on a product
 * with no bus. Each one is handed a core::SignalStore to fill and is
 * responsible for everything between the hardware and set_at().
 *
 * Lives in core/vehicle rather than core/can because there is nothing
 * bus-specific about it: it names signals by catalog id and hands back
 * nothing. That keeps the dependency arrow pointing the way Phase 1
 * established -- core/can includes this, core/vehicle never includes
 * core/can.
 *
 * The composition root holds one of these and calls tick() every loop
 * iteration; it does not know which kind it has.
 */
#pragma once

#include <stdint.h>

namespace core
{

    class ISignalSource
    {
        public:
            /**
             * How many signals a source must be able to serve at once.
             *
             * Sized for a full screen of panels, which is what
             * core::Config::priorities[] can describe. Declared on the
             * interface rather than per-implementation so every source
             * inherits the same guarantee -- a source that could hold
             * fewer would silently stop serving the panels past its cut.
             *
             * Deliberately not derived from core::MAX_PANELS: core/vehicle
             * keeps its independence from core/config, so the two are tied
             * together by a static_assert in src/main.cpp, which is the one
             * place allowed to see both.
             */
            static constexpr uint8_t MAX_ACTIVE_SIGNALS = 6;

            virtual ~ISignalSource() = default;

            /**
             * Points the source at the signals currently worth acquiring --
             * the displayed panel set. Ids not in the catalog are skipped;
             * anything past MAX_ACTIVE_SIGNALS is ignored.
             *
             * Implementations must treat an unchanged list as a no-op, so
             * callers can invoke this on a timer as a backstop without
             * paying for it.
             */
            virtual void set_active(const uint16_t *signal_ids,
                                    uint8_t         count)
                = 0;

            /**
             * Advances acquisition by one main-loop iteration. Must be
             * non-blocking: this runs in the same cooperative loop as
             * lv_timer_handler(), so anything that waits here is a dropped
             * frame.
             *
             * @param now_ms Monotonic milliseconds, from the composition
             *               root's clock. Passed in so a source need not
             *               hold an IClock of its own; one that already has
             *               a clock for other reasons may ignore it.
             */
            virtual void tick(uint32_t now_ms) = 0;
    };

} // namespace core
